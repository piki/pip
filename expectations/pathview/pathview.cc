/* Show a graph of all communications */
/* Find beginning and end times */
/* Clock synch */
/* Click on a path, show that path in the upper left */
/* Click on a task, show CDF of start times or CDF of any resource */
/* Show CDF of any resource by task, host, path, or recognizer */
/* click in the dag, highlight all other nodes with the same hostname, same thread */

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "boolarray.h"
#include "common.h"
#include "dag.h"
#include "exptree.h"
#include "graph.h"
#include "plot.h"
#include "path.h"
#include "pathstub.h"
#include "pathtl.h"
#include "workqueue.h"
#include "expect.tab.hh"

#define WID(name) glade_xml_get_widget(main_xml, name)
#define WID_D(name) glade_xml_get_widget(dagpopup_xml, name)
#define WID_T(name) glade_xml_get_widget(taskpopup_xml, name)
static GladeXML *main_xml, *dagpopup_xml = NULL, *taskpopup_xml = NULL;
enum { NOTEBOOK_TREE, NOTEBOOK_TIMELINE, NOTEBOOK_COMM, NOTEBOOK_GRAPH };
enum { QUANT_START, QUANT_REAL, QUANT_CPU, QUANT_UTIME,
	QUANT_STIME, QUANT_MAJFLT, QUANT_MINFLT, QUANT_VCS, QUANT_IVCS,
	QUANT_LATENCY, QUANT_MESSAGES, QUANT_BYTES, QUANT_DEPTH, QUANT_THREADS, QUANT_HOSTS };
enum { STYLE_CDF, STYLE_PDF, STYLE_TIME };
enum { GRAPH_NONE, GRAPH_TASKS, GRAPH_PATHS };
const char *task_quant[] = {
	"start/1000000", "(end-start)/1000", "(utime+stime)/1000", "utime/1000",
	"stime/1000", "major_fault", "minor_fault", "vol_cs", "invol_cs",
	NULL, NULL, NULL, NULL, NULL, NULL
	//!! might be nice to implement latency, messages, and bytes for tasks
};

#define MAX_GRAPH_POINTS 5000

static char *table_base;
MYSQL mysql;
static GtkListStore *list_tasks, *list_hosts, *list_paths, *list_recognizers;
static int status_bar_context;
static Path *active_path = NULL;
static timeval first_time, last_time;
static std::vector<int> match_count;
static int invalid_paths_count = 0;
static std::vector<int> resources_count;
static bool still_checking = true;
static BoolArray *recognizers_filter;
static int graph_showing = GRAPH_NONE;

static void init_times(GtkAdjustment *end_time_adj);
static void init_tasks(GtkTreeView *tree);
static void init_hosts(GtkTreeView *tree);
static void init_paths(GtkTreeView *tree);
static void init_recognizers(GtkTreeView *tree);
static void fill_dag(GtkDAG *dag, const Path *path);
static void fill_comm(GtkGraph *graph, const Path *path);
static const char *itoa(int n);
static gboolean check_all_paths(void *iter);
static void check_path(int pathid);
static void regraph(void);

extern "C" {
GtkWidget *create_dag(const char *wid);
GtkWidget *create_comm_graph(const char *wid);
GtkWidget *create_plot(const char *wid);
GtkWidget *create_pathtl(const char *wid);
gchar *zoom_format_value(GtkScale *scale, gdouble value);
void dag_zoom_value_changed(GtkRange *range);
void comm_zoom_value_changed(GtkRange *range);
void plot_point_clicked(GtkPlot *plot, GtkPlotPoint *point);
void dag_node_clicked(GtkDAG *dag, DAGNode *node);
void filter_by_recognizers(void);
void tasks_activate(void);
void tasks_graph(void);
void paths_graph(void);
void hosts_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void paths_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void recognizers_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void fill_tasks(void);
void fill_hosts(void);
void fill_paths(void);
void fill_recognizers(void);
void on_graph_quantity_changed(GtkComboBox *cb);
void on_graph_style_changed(GtkComboBox *cb);
void on_graph_logx_changed(void);
void on_aggregation_changed(GtkComboBox *cb);
void on_comm_layout_changed(GtkComboBox *cb);
void on_scale_times_toggled(GtkToggleButton *cb);
void on_dagpopup_row_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
}

static std::set<int> path_ids;
static std::map<int,PathStub*> paths;

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	if (argc != 3) {
		fprintf(stderr, "Usage:\n  %s table-base expect-file\n", argv[0]);
		return 1;
	}
	table_base = argv[1];
	if (!expect_parse(argv[2])) return 1;
  glade_init();
  main_xml = glade_xml_new("pathview.glade", "pathview_main", NULL);
  glade_xml_signal_autoconnect(main_xml);
  //gtk_object_unref(GTK_OBJECT(main_xml));
	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	char *title = g_strconcat("Path View: ", table_base, NULL);
	gtk_window_set_title(GTK_WINDOW(WID("pathview_main")), title);
	g_free(title);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("graph_quantity")), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("graph_style")), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("aggregation")), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("comm_layout")), 0);
	status_bar_context = gtk_statusbar_get_context_id(GTK_STATUSBAR(WID("statusbar")), "foo");
	gtk_tree_selection_set_mode(
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_tasks"))),
		GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_mode(
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_recognizers"))),
		GTK_SELECTION_MULTIPLE);

	init_times(GTK_RANGE(WID("end_time"))->adjustment);
	init_tasks(GTK_TREE_VIEW(WID("list_tasks")));
	init_hosts(GTK_TREE_VIEW(WID("list_hosts")));
	init_paths(GTK_TREE_VIEW(WID("list_paths")));

	get_path_ids(&mysql, table_base, &path_ids);
	get_threads(&mysql, table_base, &threads);
	// initialize all path-checking counters to zero
	invalid_paths_count = 0;
	match_count.insert(match_count.end(), recognizers.size(), 0);
	resources_count.insert(resources_count.end(), recognizers.size(), 0);
	// queue up an idler job to check all paths
	printf("checking %d paths\n", path_ids.size());
	std::set<int>::const_iterator *p = new std::set<int>::const_iterator;
	*p = path_ids.begin();
	g_idle_add_full(G_PRIORITY_LOW, check_all_paths, p, NULL);
	// initialize the recognizers box with all counts zero
	// when check_all_paths finishes, it will update this
	init_recognizers(GTK_TREE_VIEW(WID("list_recognizers")));

  gtk_main();
  return 0;
}

static std::string stringf(const char *fmt, ...) {
  char buf[4096];
  va_list arg;
  va_start(arg, fmt);
  vsprintf(buf, fmt, arg);
  va_end(arg);
	return std::string(buf);
}

static void min_time(MYSQL_ROW row, void *ign) {
	timeval temp = ts_to_tv(strtoll(row[0], NULL, 10));
	if (first_time.tv_sec == 0 || temp < first_time) first_time = temp;
}

static void max_time(MYSQL_ROW row, void *ign) {
	timeval temp = ts_to_tv(strtoll(row[0], NULL, 10));
	if (last_time.tv_sec == 0 || temp > last_time) last_time = temp;
}

static timeval limit_start, limit_end;

static void times_changed(float start_time, float end_time) {
	printf("times changed: %f to %f\n",
		gtk_range_get_value(GTK_RANGE(WID("start_time"))),
		gtk_range_get_value(GTK_RANGE(WID("end_time"))));

	limit_start = first_time + (int)(1000000*start_time);
	limit_end = first_time + (int)(1000000*end_time);
	fill_paths();
}

static void start_time_changed(void) {
	float start_time = gtk_range_get_value(GTK_RANGE(WID("start_time")));
	float end_time = gtk_range_get_value(GTK_RANGE(WID("end_time")));
	if (end_time < start_time)
		gtk_range_set_value(GTK_RANGE(WID("end_time")), start_time);
	else
		times_changed(start_time, end_time);
}

static void end_time_changed(void) {
	float start_time = gtk_range_get_value(GTK_RANGE(WID("start_time")));
	float end_time = gtk_range_get_value(GTK_RANGE(WID("end_time")));
	if (end_time < start_time)
		gtk_range_set_value(GTK_RANGE(WID("start_time")), end_time);
	else
		times_changed(start_time, end_time);
}

static void times_finished(void *data) {
	GtkAdjustment *end_time_adj = GTK_ADJUSTMENT(data);
	GtkAdjustment *start_time_adj = GTK_RANGE(WID("start_time"))->adjustment;
	start_time_adj->upper = end_time_adj->value = end_time_adj->upper = (last_time - first_time)/1000000.0;
	gtk_adjustment_changed(start_time_adj);
	gtk_adjustment_changed(end_time_adj);
	printf("first time: %ld.%06ld    last time: %ld.%06ld\n",
		first_time.tv_sec, first_time.tv_usec,
		last_time.tv_sec, last_time.tv_usec);

	g_signal_connect(G_OBJECT(end_time_adj), "value_changed", (GCallback)end_time_changed, NULL);
	g_signal_connect(G_OBJECT(start_time_adj), "value_changed", (GCallback)start_time_changed, NULL);

	limit_start = first_time;
	limit_end = last_time;
}

static void init_times(GtkAdjustment *end_time_adj) {
	add_db_idler(stringf("SELECT MIN(start) FROM %s_tasks", table_base), min_time, NULL, NULL, NULL);
	add_db_idler(stringf("SELECT MIN(ts) FROM %s_notices", table_base), min_time, NULL, NULL, NULL);
	add_db_idler(stringf("SELECT MIN(ts_send) FROM %s_messages", table_base), min_time, NULL, NULL, NULL);
	add_db_idler(stringf("SELECT MIN(ts_recv) FROM %s_messages", table_base), min_time, NULL, NULL, NULL);

	add_db_idler(stringf("SELECT MAX(start) FROM %s_tasks", table_base), max_time, NULL, NULL, NULL);
	add_db_idler(stringf("SELECT MAX(ts) FROM %s_notices", table_base), max_time, NULL, NULL, NULL);
	add_db_idler(stringf("SELECT MAX(ts_send) FROM %s_messages", table_base), max_time, NULL, NULL, NULL);
	add_db_idler(stringf("SELECT MAX(ts_recv) FROM %s_messages", table_base), max_time, NULL, times_finished, end_time_adj);
}

static void init_tasks(GtkTreeView *tree) {
	list_tasks = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_tasks), 1, GTK_SORT_DESCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_tasks));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Count", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_tasks();
}

static void init_hosts(GtkTreeView *tree) {
	list_hosts = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_hosts), 0, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_hosts));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Host", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Threads", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_hosts();
}

static void init_paths(GtkTreeView *tree) {
	list_paths = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_paths), 0, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_paths));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Path ID", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Path name", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_paths();
}

static void init_recognizers(GtkTreeView *tree) {
	list_recognizers = gtk_list_store_new(7,
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_recognizers), 1, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_recognizers));
	GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("", renderer, "active", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes("Recognizer", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	col = gtk_tree_view_column_new_with_attributes("", renderer, "text", 2, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 2);
	col = gtk_tree_view_column_new_with_attributes("", renderer, "text", 3, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 3);
	col = gtk_tree_view_column_new_with_attributes("Paths", renderer, "text", 4, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 4);
	col = gtk_tree_view_column_new_with_attributes("Resource violations", renderer, "text", 5, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 5);
	fill_recognizers();
}

static void add_task(MYSQL_ROW row, void *ign) {
	GtkTreeIter iter;
	gtk_list_store_append(list_tasks, &iter);
	gtk_list_store_set(list_tasks, &iter,
		0, row[0],
		1, atoi(row[1]),
		-1);
}

static void first_task(void *ign) {
	gtk_statusbar_push(GTK_STATUSBAR(WID("statusbar")), status_bar_context, "Reading tasks");
}

static void last_task(void *ign) {
	gtk_statusbar_pop(GTK_STATUSBAR(WID("statusbar")), status_bar_context);
}

void fill_tasks(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("tasks_search")));
	gtk_list_store_clear(list_tasks);
	std::string query("SELECT name,COUNT(name) FROM ");
	query.append(table_base).append("_tasks");
	if (search && search[0]) {
		if (search[0] == '!')
			query.append(" WHERE name NOT LIKE '%%").append(&search[1]).append("%%'");
		else
			query.append(" WHERE name LIKE '%%").append(search).append("%%'");
	}
	query.append(" GROUP BY name LIMIT 1000");
	add_db_idler(query, add_task, first_task, last_task, NULL);
}

static void add_host(MYSQL_ROW row, void *ign) {
	GtkTreeIter iter;
	gtk_list_store_append(list_hosts, &iter);
	gtk_list_store_set(list_hosts, &iter,
		0, row[0],
		1, atoi(row[1]),
		-1);
}

static void first_host(void *ign) {
	gtk_statusbar_push(GTK_STATUSBAR(WID("statusbar")), status_bar_context, "Reading hosts");
}

static void last_host(void *ign) {
	gtk_statusbar_pop(GTK_STATUSBAR(WID("statusbar")), status_bar_context);
}

void fill_hosts(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("hosts_search")));
	gtk_list_store_clear(list_hosts);
	std::string query("SELECT host,COUNT(host) FROM ");
	query.append(table_base).append("_threads");
	if (search && search[0]) {
		if (search[0] == '!')
			query.append(" WHERE host NOT LIKE '%%").append(&search[1]).append("%%'");
		else
			query.append(" WHERE host LIKE '%%").append(search).append("%%'");
	}
	query.append(" GROUP BY host");
	add_db_idler(query, add_host, first_host, last_host, NULL);
}

static bool should_show(const PathStub *ps) {
	if (ps && recognizers_filter) {
		int count = recognizers_filter->intersect_count(ps->recognizers, recognizers.size()+1);
		bool seek_inval = (*recognizers_filter)[recognizers.size()];
		if (count == 0 && !(seek_inval && !ps->validated)) return false;
	}
	return true;
}

static std::vector<int> shown_paths;
static void add_path(MYSQL_ROW row, void *ign) {
	int pathid = atoi(row[0]);
	PathStub *ps = paths[pathid];
	if (!still_checking) {
		if (!ps) return;  // empty path
		assert(ps->path_id == pathid);
		if (ps->ts_end < limit_start || ps->ts_start > limit_end) return;
	}
	if (!should_show(ps)) return;
	GtkTreeIter iter;
	gtk_list_store_append(list_paths, &iter);
	gtk_list_store_set(list_paths, &iter,
		0, pathid,
		1, row[1],
		-1);
	shown_paths.push_back(pathid);
}

static void first_path(void *ign) {
	gtk_statusbar_push(GTK_STATUSBAR(WID("statusbar")), status_bar_context, "Reading paths");
}

static void last_path(void *ign) {
	gtk_statusbar_pop(GTK_STATUSBAR(WID("statusbar")), status_bar_context);
}

void fill_paths(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("paths_search")));
	gtk_list_store_clear(list_paths);
	std::string query("SELECT pathid,pathblob FROM ");
	query.append(table_base).append("_paths");
	if (search && search[0]) {
		if (search[0] == '!')
			query.append(" WHERE pathblob NOT LIKE '%%").append(&search[1]).append("%%'");
		else
			query.append(" WHERE pathblob LIKE '%%").append(search).append("%%'");
	}
	query += " LIMIT 5000";
	shown_paths.clear();
	add_db_idler(query, add_path, first_path, last_path, NULL);
}

void fill_recognizers(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("recognizers_search")));
	gtk_list_store_clear(list_recognizers);
	GtkTreeIter iter;
	std::map<std::string, Recognizer*>::const_iterator rp;
	int i;
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++)
		if (!search || !search[0] || rp->first.find(search) != std::string::npos) {
			char pathtype[] = "X";
			pathtype[0] = toupper(path_type_to_string(rp->second->pathtype)[0]);
			gtk_list_store_append(list_recognizers, &iter);
			gtk_list_store_set(list_recognizers, &iter,
				0, FALSE,
				1, rp->first.c_str(),
				2, pathtype,
				3, rp->second->complete ? "C" : "F",
				4, match_count[i],
				5, resources_count[i],
				6, i,
				-1);
		}
	gtk_list_store_append(list_recognizers, &iter);
	gtk_list_store_set(list_recognizers, &iter,
		1, "(unvalidated)",
		4, invalid_paths_count,
		6, recognizers.size(),
		-1);
}

#define SLIDER_TO_ZOOM(z) pow(2, (z)-3)
#define DEFAULT_ZOOM 0.6  /* slider value: 2.27 */
GtkWidget *create_dag(const char *wid) {
	GtkWidget *dag = gtk_dag_new();
	gtk_dag_set_zoom(GTK_DAG(dag), DEFAULT_ZOOM);
	gtk_widget_show(dag);

	return dag;
}

GtkWidget *create_comm_graph(const char *wid) {
	GtkWidget *graph = gtk_graph_new();
	gtk_graph_set_zoom(GTK_GRAPH(graph), DEFAULT_ZOOM);
	gtk_widget_show(graph);
	return graph;
}

GtkWidget *create_plot(const char *wid) {
	GtkWidget *plot = gtk_plot_new((GtkPlotFlags)(PLOT_DEFAULTS|PLOT_Y0));
	gtk_widget_show(plot);
	return plot;
}

GtkWidget *create_pathtl(const char *wid) {
	GtkWidget *pathtl = gtk_pathtl_new();
	gtk_widget_show(pathtl);
	return pathtl;
}

gchar *zoom_format_value(GtkScale *scale, gdouble value) {
	return g_strdup_printf("%.2f", SLIDER_TO_ZOOM(value));
}

void dag_zoom_value_changed(GtkRange *range) {
	gtk_dag_set_zoom(GTK_DAG(WID("dag")),
		SLIDER_TO_ZOOM(gtk_range_get_value(range)));
}

void comm_zoom_value_changed(GtkRange *range) {
	gtk_graph_set_zoom(GTK_GRAPH(WID("comm_graph")),
		SLIDER_TO_ZOOM(gtk_range_get_value(range)));
}

void plot_point_clicked(GtkPlot *plot, GtkPlotPoint *point) {
	char buf[64];
	if (point) {
		sprintf(buf, "(%.3f, %.3f)", point->x, point->y);
		gtk_label_set_text(GTK_LABEL(WID("graph_pos")), buf);
		if (graph_showing == GRAPH_PATHS && point->user_data) {
			PathStub *ps = (PathStub*)point->user_data;
			printf("path = %p { id=%d start=%ld.%06ld end=%ld.%06ld }\n",
				ps, ps->path_id, ps->ts_start.tv_sec, ps->ts_start.tv_usec,
				ps->ts_end.tv_sec, ps->ts_end.tv_usec);
		}
	}
	else
		gtk_label_set_text(GTK_LABEL(WID("graph_pos")), "(X, Y)");
}

static GtkTreeStore *popup_events;
static void popup_append_event(const PathEvent *ev, GtkTreeIter *iter, GtkTreeIter *parent) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%ld.%06ld", ev->start().tv_sec, ev->start().tv_usec);
	std::string txt = ev->to_string();
	gtk_tree_store_append(popup_events, iter, parent);
	gtk_tree_store_set(popup_events, iter,
		0, buf,
		1, txt.c_str(),
		2, ev->type() == PEV_TASK ? ev : NULL,
		-1);
}

// returns true if the given recv 'match' is somewhere in 'list' or its
// children
static bool popup_match_recv(const PathEventList &list, PathMessageRecv *match) {
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				if (popup_match_recv(((PathTask*)list[i])->children, match)) return true;
				break;
			case PEV_MESSAGE_RECV:
				if (list[i] == match) return true;
				break;
			default: ;
		}
	}
	return false;
}

static bool popup_fill_events(const PathEventList &list, PathMessageRecv *match, bool *found_start, GtkTreeIter *parent) {
	GtkTreeIter iter;
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				// this task is important if we've already gotten the recv, _or_
				// if the recv is one of our descendants
				if (*found_start || popup_match_recv(((PathTask*)list[i])->children, match))
					popup_append_event(list[i], &iter, parent);
				if (popup_fill_events(((PathTask*)list[i])->children, match, found_start, &iter))
					return true;  // if a child says we're done, we're done
				break;
			case PEV_MESSAGE_RECV:
				if (*found_start) return true;  // found the next recv; we're done
				if (list[i] == match) {
					*found_start = TRUE;
					popup_append_event(list[i], &iter, parent);
				}
				break;
			default: ;
				if (*found_start) popup_append_event(list[i], &iter, parent);
		}
	}
	return false;
}

void dag_node_clicked(GtkDAG *dag, DAGNode *node) {
	if (!dagpopup_xml) {
		dagpopup_xml = glade_xml_new("pathview.glade", "pathview_dagpopup", NULL);
		glade_xml_signal_autoconnect(dagpopup_xml);

		GtkTreeView *tree = GTK_TREE_VIEW(WID_D("dagpopup_list"));
		popup_events = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
		gtk_tree_view_set_model(tree, GTK_TREE_MODEL(popup_events));
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
		GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Time", renderer, "text", 0, NULL);
		gtk_tree_view_append_column(tree, col);
		col = gtk_tree_view_column_new_with_attributes("Event", renderer, "text", 1, NULL);
		gtk_tree_view_append_column(tree, col);
	}
	else {
		printf("showing dagpopup\n");
		gtk_widget_show(WID_D("pathview_dagpopup"));
	}

	PathMessageRecv *pmr = (PathMessageRecv*)node->user_data;
	int thread_id = pmr ? pmr->thread_recv : active_path->root_thread;
	run_sqlf(&mysql, "SELECT thread_id,host,prog FROM %s_threads WHERE thread_id=%d", table_base, thread_id);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row = mysql_fetch_row(res);
	assert(row);
	assert(atoi(row[0]) == thread_id);
	gtk_label_set_text(GTK_LABEL(WID_D("label_thread")), itoa(thread_id));
	gtk_label_set_text(GTK_LABEL(WID_D("label_host")), row[1]);
	gtk_label_set_text(GTK_LABEL(WID_D("label_program")), row[2]);
	gtk_label_set_text(GTK_LABEL(WID("label_host")), row[1]);
	gtk_label_set_text(GTK_LABEL(WID("label_program")), row[2]);
	mysql_free_result(res);

	gtk_tree_store_clear(popup_events);
	bool found_start = pmr == NULL;
	popup_fill_events(active_path->children.find(thread_id)->second, pmr, &found_start, NULL);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(WID_D("dagpopup_list")));
}

void each_recognizer(GtkTreeModel *ign1, GtkTreePath *ign2, GtkTreeIter *iter,
		gpointer data) {
	int recog;
	gtk_tree_model_get(GTK_TREE_MODEL(list_recognizers), iter, 6, &recog, -1);
	recognizers_filter->set(recog, true);
	(*(int*)data)++;
}

void filter_by_recognizers(void) {
	if (recognizers_filter) delete recognizers_filter;
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(WID("filter_toggle")))) {
		recognizers_filter = NULL;
		fill_paths();
		return;
	}
	recognizers_filter = new BoolArray(recognizers.size()+1);
	GtkTreeSelection *sel =
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_recognizers")));
	int count = 0;
	gtk_tree_selection_selected_foreach(sel, each_recognizer, &count);
	if (count == 0) {
		delete recognizers_filter;
		recognizers_filter = NULL;
	}
	fill_paths();
}

static void tasks_plot_row(GtkTreeModel *ign1, GtkTreePath *ign2, GtkTreeIter *iter,
		gpointer data) {
	int quant = gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_quantity")));
	int style = gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_style")));
	if (!task_quant[quant]) {
		printf("quant %d is not defined for tasks (yet?)\n", quant);
		return;
	}

	const char *taskname;
	gtk_tree_model_get(GTK_TREE_MODEL(list_tasks), iter, 0, &taskname, -1);

	GtkPlot *plot = GTK_PLOT(data);
	gtk_plot_start_new_line(plot);

	int row_count, skip;
	MYSQL_RES *res;
	MYSQL_ROW row;
	if (style == STYLE_CDF || style == STYLE_TIME) {
		run_sqlf(&mysql, "SELECT COUNT(*) FROM %s_tasks WHERE name='%s'",
			table_base, taskname);
		res = mysql_use_result(&mysql);
		row = mysql_fetch_row(res);
		row_count = atoi(row[0]);
		skip = row_count / (MAX_GRAPH_POINTS - 1) + 1;
		mysql_free_result(res);
	}

	char *query;
	switch (style) {
		case STYLE_PDF:
			query = g_strdup_printf("SELECT ROUND(%s%s) AS x,COUNT(name) FROM %s_tasks WHERE name='%s' GROUP BY x",
				task_quant[quant], quant == QUANT_START ? itoa(-first_time.tv_sec) : "",
				table_base, taskname);
			break;
		case STYLE_CDF:
			query = g_strdup_printf("SELECT %s%s AS x FROM %s_tasks WHERE name='%s' ORDER BY x",
				task_quant[quant], quant == QUANT_START ? itoa(-first_time.tv_sec) : "",
				table_base, taskname);
			break;
		case STYLE_TIME:
			query = g_strdup_printf("SELECT %s%s AS x,start/1000000-%ld FROM %s_tasks WHERE name='%s' ORDER BY start",
				task_quant[quant], quant == QUANT_START ? itoa(-first_time.tv_sec) : "",
				first_time.tv_sec, table_base, taskname);
			break;
	}
	run_sql(&mysql, query);
	printf("sql(\"%s\")\n", query);
	g_free(query);
	res = mysql_use_result(&mysql);
	int n = 0, x, last_x = 1<<30;
	while ((row = mysql_fetch_row(res)) != NULL) {
		switch (style) {
			case STYLE_CDF:
				if (n % skip == 0 || n == row_count - 1)
					gtk_plot_add_point(plot, atof(row[0]), (double)n/(row_count-1), NULL);
				n++;
				break;
			case STYLE_PDF:
				x = atoi(row[0]);
				if (x == last_x + 2)
					gtk_plot_add_point(plot, x-1, 0, NULL);
				else if (x > last_x + 2) {
					gtk_plot_add_point(plot, last_x+1, 0, NULL);
					gtk_plot_add_point(plot, x-1, 0, NULL);
				}
				last_x = x;
				gtk_plot_add_point(plot, x, atof(row[1]), NULL);
				break;
			case STYLE_TIME:
				if (n % skip == 0 || n == row_count - 1)
					gtk_plot_add_point(plot, atof(row[1]), atof(row[0]), NULL);
				n++;
				break;
		}
	}
	mysql_free_result(res);
}

void graph_common(const char *title, int _graph_showing) {
	if (title) gtk_label_set_text(GTK_LABEL(WID("graph_title")), title);
	graph_showing = _graph_showing;
	GtkPlot *plot = GTK_PLOT(WID("plot"));
	gtk_plot_freeze(plot);
	int flags = (int)plot->flags;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(WID("logx")))) flags |= PLOT_LOGSCALE_X; else flags &= ~PLOT_LOGSCALE_X;
	gtk_plot_set_flags(plot, (GtkPlotFlags)flags);
	gtk_plot_clear(plot);
}

void tasks_activate(void) {
	tasks_graph();
}

void tasks_graph(void) {
	graph_common("Performance: Tasks", GRAPH_TASKS);
	GtkTreeSelection *sel =
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_tasks")));
	GtkPlot *plot = GTK_PLOT(WID("plot"));
	gtk_tree_selection_selected_foreach(sel, tasks_plot_row, plot);
	gtk_plot_thaw(plot);
	if (plot->lines->len > 0)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(WID("notebook")), NOTEBOOK_GRAPH);
}

struct PlotPoint {
	float x, y;
	PathStub *ps;
};

struct PlotPointX {
	float x;
	PathStub *ps;
};

template<typename T>
static int cmp(const void *a, const void *b) {
	if (*(T*)a < *(T*)b) return -1;
	if (*(T*)a > *(T*)b) return 1;
	return 0;
}

void paths_graph(void) {
	graph_common("Performance: Paths", GRAPH_PATHS);

	GtkPlot *plot = GTK_PLOT(WID("plot"));

	int quant = gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_quantity")));
	int style = gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_style")));

	if (quant != QUANT_START && quant != QUANT_REAL && quant != QUANT_CPU &&
			quant != QUANT_UTIME && quant != QUANT_STIME && quant != QUANT_MAJFLT &&
			quant != QUANT_MINFLT && quant != QUANT_VCS && quant != QUANT_IVCS &&
			quant != QUANT_MESSAGES && quant != QUANT_DEPTH && quant != QUANT_HOSTS &&
			quant != QUANT_BYTES && quant != QUANT_THREADS) {
		gtk_plot_thaw(plot);
		printf("quant %d is not defined for paths (yet?)\n", quant);
		return;
	}

	gtk_plot_start_new_line(plot);

	union {
		PlotPoint *pp;
		unsigned int *i;
		PlotPointX *f;
	} points;
	switch (style) {
		case STYLE_CDF:   points.f = new PlotPointX[shown_paths.size()];    break;
		case STYLE_PDF:   points.i = new unsigned int[shown_paths.size()];  break;
		case STYLE_TIME:  points.pp = new PlotPoint[shown_paths.size()];    break;
	}
	unsigned int npoints = 0;
	for (unsigned int i=0; i<shown_paths.size(); i++) {
		PathStub *p = paths[i];
		if (!p) continue;  // path has not been read+checked yet
		float val = 0.0;
		switch (quant) {
			case QUANT_START:     val = (p->ts_start - first_time) / 1000000.0;  break;
			case QUANT_REAL:      val = (p->ts_end - p->ts_start) / 1000.0;      break;
			case QUANT_CPU:       val = (p->stime + p->utime) / 1000.0;          break;
			case QUANT_UTIME:     val = p->utime / 1000.0;                       break;
			case QUANT_STIME:     val = p->stime / 1000.0;                       break;
			case QUANT_MAJFLT:    val = p->major_fault;                          break;
			case QUANT_MINFLT:    val = p->minor_fault;                          break;
			case QUANT_VCS:       val = p->vol_cs;                               break;
			case QUANT_IVCS:      val = p->invol_cs;                             break;
			case QUANT_MESSAGES:  val = p->messages;                             break;
			case QUANT_DEPTH:     val = p->depth;                                break;
			case QUANT_HOSTS:     val = p->hosts;                                break;
			case QUANT_BYTES:     val = p->bytes;                                break;
			case QUANT_THREADS:   val = p->threads;                              break;
			case QUANT_LATENCY:
				fprintf(stderr, "Quant %d not implemented for paths yet\n", quant);
				break;
			default: assert(!"invalid quant");
		}
		assert(val >= 0);
		switch (style) {
			case STYLE_CDF:
				points.f[npoints].x = val;
				points.f[npoints].ps = p;
				break;
			case STYLE_PDF:
				points.i[npoints] = (int)(val + 0.5);
				break;
			case STYLE_TIME:
				points.pp[npoints].x = (p->ts_start - first_time)/1000000.0;
				points.pp[npoints].y = val;
				points.pp[npoints].ps = p;
				break;
		}
		npoints++;
	}
	unsigned int j, last_x = 1<<30;
	int skip = npoints / (MAX_GRAPH_POINTS - 1) + 1;
	switch (style) {
		case STYLE_CDF:
			qsort(points.f, npoints, sizeof(PlotPointX), &cmp<float>);
			for (unsigned int i=0; i<npoints; i+=skip)
				gtk_plot_add_point(plot, points.f[i].x, (double)i/(npoints-1), points.f[i].ps);
			if ((npoints-1) % skip != 0)
				gtk_plot_add_point(plot, points.f[npoints-1].x, 1.0, points.f[npoints-1].ps);
			delete[] points.f;
			break;
		case STYLE_PDF:
			qsort(points.i, npoints, sizeof(unsigned int), &cmp<unsigned int>);
			// use 'j' to count how many identical values appear in a row, then
			// plot that count as the Y value
			for (unsigned int i=0; i<npoints; ) {
				for (j=i+1; j<npoints && points.i[j] == points.i[i]; j++)  ;
				if (points.i[i] == last_x + 2)
					gtk_plot_add_point(plot, points.i[i]-1, 0, NULL);
				else if (points.i[i] > last_x + 2) {
					gtk_plot_add_point(plot, last_x+1, 0, NULL);
					gtk_plot_add_point(plot, points.i[i]-1, 0, NULL);
				}
				last_x = points.i[i];
				gtk_plot_add_point(plot, points.i[i], j-i, NULL);
				i=j;
			}
			delete[] points.i;
			break;
		case STYLE_TIME:
			qsort(points.pp, npoints, sizeof(PlotPoint), &cmp<float>);
			for (unsigned int i=0; i<npoints; i++)
				gtk_plot_add_point(plot, points.pp[i].x, points.pp[i].y, points.pp[i].ps);
			delete[] points.pp;
			break;
	}

	gtk_plot_thaw(plot);
	if (plot->lines->len > 0)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(WID("notebook")), NOTEBOOK_GRAPH);
}

void hosts_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	printf("hosts_activate\n");
}

static gboolean set_if_matched(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
	assert(model == GTK_TREE_MODEL(list_recognizers));
	PathStub *ps = (PathStub*)data;
	if (ps) {
		int rid;
		gtk_tree_model_get(model, iter, 6, &rid, -1);
		bool matched = ps->recognizers[rid];
		gtk_list_store_set(list_recognizers, iter, 0, matched, -1);
	}
	else
		gtk_list_store_set(list_recognizers, iter, 0, FALSE, -1);
	
	return FALSE;
}

void paths_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	GtkTreeIter iter;
	int pathid;
	char *pathname;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(list_paths), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(list_paths), &iter, 0, &pathid, 1, &pathname, -1);
	PathStub *ps = paths[pathid];
	// call it even if ps==NULL -- it will just set to false
	gtk_tree_model_foreach(GTK_TREE_MODEL(list_recognizers), set_if_matched, ps);
	if (active_path) delete active_path;
	active_path = new Path(&mysql, table_base, pathid);
	if (!active_path->valid()) {
		printf("malformed path -- not displaying\n");
		delete active_path;
		active_path = NULL;
		return;
	}
	char *buf = g_strdup_printf("%s (%d)", pathname, pathid);
	g_free(pathname);
	gtk_label_set_text(GTK_LABEL(WID("label_path")), buf);
	g_free(buf);
	gtk_pathtl_set(GTK_PATHTL(WID("pathtl")), active_path);
	fill_dag(GTK_DAG(WID("dag")), active_path);
	fill_comm(GTK_GRAPH(WID("comm_graph")), active_path);
}

void recognizers_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(WID("filter_toggle"))))
		filter_by_recognizers();
	else
		// this ends up calling filter_by_recognizers on its own
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(WID("filter_toggle")), TRUE);
}

void on_graph_quantity_changed(GtkComboBox *cb) {
	regraph();
}

void on_graph_style_changed(GtkComboBox *cb) {
	regraph();
}

void on_graph_logx_changed(void) { regraph(); }

void on_aggregation_changed(GtkComboBox *cb) {
	printf("aggregation changed to %d/%s\n", gtk_combo_box_get_active(cb), gtk_combo_box_get_active_text(cb));
}

void on_comm_layout_changed(GtkComboBox *cb) {
	char *cmd = g_strdup(gtk_combo_box_get_active_text(cb));
	char *p = strchr(cmd, ' ');
	if (p) *p = '\0';
	gtk_graph_set_layout_program(GTK_GRAPH(WID("comm_graph")), cmd);
	g_free(cmd);
}

void on_scale_times_toggled(GtkToggleButton *cb) {
	gtk_pathtl_set_times_to_scale(GTK_PATHTL(WID("pathtl")), gtk_toggle_button_get_active(cb));
}

static std::map<const PathMessageRecv*, DAGNode*> dag_nodes;

static const char *itoa(int n) {
	static char buf[15];
	sprintf(buf, "%d", n);
	return buf;
}

static DAGNode *fill_dag_one_message(GtkDAG *dag, const PathMessageRecv *pmr, const Path *path) {
	if (dag_nodes[pmr]) return dag_nodes[pmr];
	DAGNode *parent = dag_nodes[pmr->send->pred];
	assert(parent);
	dag_nodes[pmr] = gtk_dag_add_node(dag, itoa(pmr->thread_recv), parent, NULL, 0, (void*)pmr);
	return dag_nodes[pmr];
}

// returns true if the end of interval (a second recv) is found
static bool fill_dag_from_list(GtkDAG *dag, const PathEventList &list, const Path *path,
		const PathMessageRecv *match, bool *found_start) {
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				if (fill_dag_from_list(dag, ((PathTask*)list[i])->children, path, match, found_start))
					return true;
				break;
			case PEV_MESSAGE_RECV:
				if (*found_start) return true;
				if (list[i] == match) *found_start = true;
				break;
			case PEV_MESSAGE_SEND:
				if (*found_start) {
					bool child_start = false;
					const PathMessageSend *pms = (PathMessageSend*)list[i];
					fill_dag_one_message(dag, pms->recv, path);
					// don't care if receiver of this message ends his interval
					(void)fill_dag_from_list(dag, path->children.find(pms->recv->thread_recv)->second, path,
						pms->recv, &child_start);
				}
				break;
			default: ;
		}
	}
	return false;
}

static void fill_dag(GtkDAG *dag, const Path *path) {
	dag_nodes.clear();
	gtk_dag_freeze(dag);
	gtk_dag_clear(dag);
	dag_nodes[NULL] = gtk_dag_add_root(dag, itoa(path->root_thread), NULL, 0, NULL);
	bool found_start = TRUE;
	fill_dag_from_list(dag, path->children.find(path->root_thread)->second, path, NULL, &found_start);
	gtk_dag_thaw(dag);
}

static void fill_comm(GtkGraph *graph, const Path *path) {
	run_sqlf(&mysql, "SELECT DISTINCT host1.host,host2.host "
		"FROM %s_messages,%s_threads AS host1,%s_threads AS host2 "
		"WHERE %s_messages.thread_send=host1.thread_id "
			"AND %s_messages.thread_recv=host2.thread_id "
			"AND %s_messages.pathid=%d",
		table_base, table_base, table_base, table_base, table_base, table_base, path->path_id);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	std::map<std::string, GtkGraphNode*> nodes;
	gtk_graph_freeze(graph);
	gtk_graph_clear(graph);
	while ((row = mysql_fetch_row(res)) != NULL) {
		if (nodes.find(row[0]) == nodes.end()) {
			nodes[row[0]] = gtk_graph_add_node(graph, row[0]);
		}
		if (nodes.find(row[1]) == nodes.end()) {
			nodes[row[1]] = gtk_graph_add_node(graph, row[1]);
		}

		(void)gtk_graph_add_edge(graph, nodes[row[0]], nodes[row[1]], TRUE);
	}
	gtk_graph_simplify(graph);
	mysql_free_result(res);
	gtk_graph_thaw(graph);
}

static gboolean check_all_paths(void *iter) {
	std::set<int>::const_iterator *p = (std::set<int>::const_iterator*)iter;
	check_path(**p);
	if (++(*p) == path_ids.end()) {
		printf("done checking paths\n");
		delete p;
		fill_paths();
		fill_recognizers();
		still_checking = false;
		return false;
	}
	return true;
}

static void check_path(int pathid) {
	std::map<std::string,Recognizer*>::const_iterator rp;
	unsigned int i;
	Path path(&mysql, table_base, pathid);
	PathStub *ps = paths[pathid] = new PathStub(path, recognizers.size()+1);

	if (!path.valid()) {
		printf("# path %d malformed -- not checked\n", pathid);
		//malformed_paths_count++;
		return;
	}
	int validators_matched = 0;
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++) {
		bool resources = true;
		if (rp->second->check(&path, &resources)) {
			if (resources) {
				ps->recognizers.set(i, true);
				match_count[i]++;
				if (rp->second->pathtype == VALIDATOR) {
					ps->validated = true;
					validators_matched++;
				}
			}
			else
				resources_count[i]++;
		}
		else
			ps->recognizers.set(i, false);
	}
	// invalidators override validators.  do that here.
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++)
		if (rp->second->pathtype == INVALIDATOR && ps->recognizers[i]) {
			ps->validated = false;
			// This may be misleading because some validators MAY have been
			// matched.  It's OK, because the only thing this variable is used
			// for is incrementing invalid_paths_count.
			validators_matched = 0;
			break;
		}
	if (validators_matched == 0) invalid_paths_count++;
}

static void regraph(void) {
	switch (graph_showing) {
		case GRAPH_NONE:                               break;
		case GRAPH_TASKS:        tasks_graph();        break;
		case GRAPH_PATHS:        paths_graph();        break;
	}
}

static GtkListStore *taskpopup_props;
static void taskpopup_add_item(const char *label, const char *fmt, ...) {
	GtkTreeIter iter;
	char buf[64];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	gtk_list_store_append(taskpopup_props, &iter);
	gtk_list_store_set(taskpopup_props, &iter,
		0, label,
		1, buf,
		-1);
	va_end(args);
}
void on_dagpopup_row_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	printf("dagpopup_row_activated\n");
	GtkTreeIter iter;
	PathEvent *ev;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(popup_events), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(popup_events), &iter, 2, &ev, -1);
	if (!ev) return;  // not a task
	assert(ev->type() == PEV_TASK);
	PathTask *task = (PathTask*)ev;

	if (!taskpopup_xml) {
		taskpopup_xml = glade_xml_new("pathview.glade", "pathview_taskpopup", NULL);
		glade_xml_signal_autoconnect(taskpopup_xml);

		GtkTreeView *tree = GTK_TREE_VIEW(WID_T("taskpopup_list"));
		taskpopup_props = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
		gtk_tree_view_set_model(tree, GTK_TREE_MODEL(taskpopup_props));
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
		GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
		gtk_tree_view_append_column(tree, col);
		col = gtk_tree_view_column_new_with_attributes("Value", renderer, "text", 1, NULL);
		gtk_tree_view_append_column(tree, col);
	}
	else {
		printf("showing taskpopup\n");
		gtk_widget_show(WID_T("pathview_taskpopup"));
		gtk_list_store_clear(taskpopup_props);
	}

	taskpopup_add_item("Start time", "%ld.%06ld", task->ts_start.tv_sec, task->ts_start.tv_usec);
	taskpopup_add_item("End time", "%ld.%06ld", task->ts_end.tv_sec, task->ts_end.tv_usec);
	taskpopup_add_item("Real time (ms)", "%.3f", task->tdiff/1000.0);
	taskpopup_add_item("System time (ms)", "%.3f", task->stime/1000.0);
	taskpopup_add_item("User time (ms)", "%.3f", task->utime/1000.0);
	taskpopup_add_item("Busy %", "%.3f", (task->utime+task->stime)/(float)task->tdiff);
	taskpopup_add_item("Major faults", "%d", task->major_fault);
	taskpopup_add_item("Minor faults", "%d", task->minor_fault);
	taskpopup_add_item("Voluntary context switches", "%d", task->vol_cs);
	taskpopup_add_item("Involuntary context switches", "%d", task->invol_cs);
	taskpopup_add_item("Starting thread", "%d", task->thread_start);
	taskpopup_add_item("End thread", "%d", task->thread_end);
}
