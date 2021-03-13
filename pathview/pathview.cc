/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

/* Clock synch */
/* click in the dag, highlight all other nodes with the same hostname, same thread */

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "boolarray.h"
#include "common.h"
#include "dag.h"
#include "exptree.h"
#include "plot.h"
#include "path.h"
#include "pathfactory.h"
#include "pathstub.h"
#include "pathtl.h"
#include "expect.tab.hh"
#include "rcfile.h"
#ifdef HAVE_RSVG
#include "graph.h"
#endif

#define MAX_PATHS_DISPLAYED 5000

#define RID_UNVALIDATED -1
/* Task columns */        enum { T_COL_COUNT, T_COL_NAME };
/* Thread-pool columns */ enum { PO_COL_THIS_PATH, PO_COL_ID, PO_COL_HOST, PO_COL_COUNT };
/* Recognizer columns */  enum { R_COL_PATH_MATCH, R_COL_NAME, R_COL_TYPE,
                                 R_COL_COMPLETE, R_COL_MATCH_COUNT,
                                 R_COL_RESOURCES, R_COL_RID };
/* Path columns */        enum { P_COL_ID, P_COL_NAME };
/* Task-popup columns */  enum { TP_COL_NAME, TP_COL_VALUE };
/* Dag-popup columns */   enum { DP_COL_TIME, DP_COL_NAME, DP_COL_EVENT };

#define WID(name) glade_xml_get_widget(main_xml, name)
#define WID_D(name) glade_xml_get_widget(dagpopup_xml, name)
#define WID_T(name) glade_xml_get_widget(taskpopup_xml, name)
static GladeXML *main_xml, *dagpopup_xml = NULL, *taskpopup_xml = NULL;
enum { NOTEBOOK_TREE, NOTEBOOK_TIMELINE, NOTEBOOK_COMM, NOTEBOOK_GRAPH };
enum { GRAPH_NONE, GRAPH_TASKS, GRAPH_PATHS };

static int max_graph_points = 256;
static char *table_base;
static PathFactory *pf;
static GtkListStore *list_tasks, *list_pools, *list_paths, *list_recognizers;
static Path *active_path = NULL;
static timeval first_time;
static std::vector<int> match_count;
static int invalid_paths_count = 0;
static std::vector<int> resources_count;
static bool still_checking = true;
static BoolArray *recognizers_filter;
static int graph_showing = GRAPH_NONE;
static const char *glade_file;

static void init_times(void);
static void init_tasks(GtkTreeView *tree);
static void init_pools(GtkTreeView *tree);
static void init_paths(GtkTreeView *tree);
static void init_recognizers(GtkTreeView *tree);
static void fill_dag(GtkDAG *dag, const Path *path);
#ifdef HAVE_RSVG
static void fill_comm(GtkGraph *graph, const Path *path);
#endif
static const char *itoa(int n);
static gboolean check_all_paths(void *iter);
static void check_path(int pathid);
static void regraph(void);
static const char *find_glade_file(void);
static void set_statusbar(const char *fmt, ...);

extern "C" {
G_MODULE_EXPORT GtkWidget *create_dag(const char *wid);
G_MODULE_EXPORT GtkWidget *create_comm_graph(const char *wid);
G_MODULE_EXPORT GtkWidget *create_plot(const char *wid);
G_MODULE_EXPORT GtkWidget *create_pathtl(const char *wid);
G_MODULE_EXPORT gchar *zoom_format_value(GtkScale *scale, gdouble value);
G_MODULE_EXPORT gchar *zoom_format_value_int(GtkScale *scale, gdouble value);
G_MODULE_EXPORT gchar *zoom_format_value_pathtl(GtkScale *scale, gdouble value);
G_MODULE_EXPORT void dag_zoom_value_changed(GtkRange *range);
G_MODULE_EXPORT void pathtl_zoom_value_changed(GtkRange *range);  // slider moved
G_MODULE_EXPORT void pathtl_zoom_changed(GtkPathTL *pathtl, double zoom);  // pathtl picked a new auto zoom
G_MODULE_EXPORT void pathtl_node_clicked(GtkPathTL *pathtl, const PathEvent *pev);
G_MODULE_EXPORT void pathtl_node_activated(GtkPathTL *pathtl, const PathEvent *pev);
G_MODULE_EXPORT void comm_zoom_value_changed(GtkRange *range);
G_MODULE_EXPORT void max_points_value_changed(GtkRange *range);
G_MODULE_EXPORT void plot_point_clicked(GtkPlot *plot, GtkPlotPoint *point);
G_MODULE_EXPORT void dag_node_clicked(GtkDAG *dag, DAGNode *node);
G_MODULE_EXPORT void dag_node_activated(GtkDAG *dag, DAGNode *node);
G_MODULE_EXPORT void filter_by_recognizers(void);
G_MODULE_EXPORT void debug_mismatch(void);
G_MODULE_EXPORT void tasks_activated(void);
G_MODULE_EXPORT void tasks_graph(void);
G_MODULE_EXPORT void paths_graph(void);
G_MODULE_EXPORT void pools_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
G_MODULE_EXPORT void paths_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
G_MODULE_EXPORT void recognizers_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
G_MODULE_EXPORT void fill_tasks(void);
G_MODULE_EXPORT void fill_pools(void);
G_MODULE_EXPORT void fill_paths(void);
G_MODULE_EXPORT void fill_recognizers(void);
G_MODULE_EXPORT void on_graph_quantity_changed(GtkComboBox *cb);
G_MODULE_EXPORT void on_graph_style_changed(GtkComboBox *cb);
G_MODULE_EXPORT void on_graph_logx_changed(void);
G_MODULE_EXPORT void on_comm_layout_changed(GtkComboBox *cb);
G_MODULE_EXPORT void pathtl_flags_changed(void);
G_MODULE_EXPORT void on_dagpopup_row_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
G_MODULE_EXPORT void bounce_quit(void) { gtk_main_quit(); }
G_MODULE_EXPORT void bounce_hide(GtkWidget *w) { gtk_widget_hide(w); }
}

static std::vector<int> path_ids;
static std::map<int,PathStub*> paths;

int main(int argc, char **argv) {
	glade_file = find_glade_file();
	gtk_init(&argc, &argv);
	if (argc != 3) {
		fprintf(stderr, "Usage:\n  %s table-base expect-file\n", argv[0]);
		return 1;
	}
	table_base = argv[1];
	if (!expect_parse(argv[2])) return 1;
	glade_init();
	main_xml = glade_xml_new(glade_file, "pathview_main", NULL);
	if (!main_xml) {
		fputs("\n\nGlade file not found, or corrupt.  Run `make install', or execute\n", stderr);
		fputs("pathview from the pathview build directory.\n\n", stderr);
		exit(1);
	}
	glade_xml_signal_autoconnect(main_xml);
	//gtk_object_unref(GTK_OBJECT(main_xml));
	pf = path_factory(table_base);
	if (!pf->valid()) return 1;

	char *title = g_strconcat("Path View: ", table_base, NULL);
	gtk_window_set_title(GTK_WINDOW(WID("pathview_main")), title);
	g_free(title);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("graph_quantity")), (int)QUANT_REAL);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("graph_style")), (int)STYLE_CDF);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("comm_layout")), 0);
	gtk_tree_selection_set_mode(
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_tasks"))),
		GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_mode(
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_recognizers"))),
		GTK_SELECTION_MULTIPLE);

	init_times();
	init_tasks(GTK_TREE_VIEW(WID("list_tasks")));
	init_pools(GTK_TREE_VIEW(WID("list_pools")));
	init_paths(GTK_TREE_VIEW(WID("list_paths")));

	path_ids = pf->get_path_ids();
	// initialize all path-checking counters to zero
	invalid_paths_count = 0;
	match_count.insert(match_count.end(), recognizers.size(), 0);
	resources_count.insert(resources_count.end(), recognizers.size(), 0);
	// queue up an idler job to check all paths
	set_statusbar("checking %d paths", path_ids.size());
	std::vector<int>::const_iterator *p = new std::vector<int>::const_iterator;
	*p = path_ids.begin();
	g_idle_add_full(G_PRIORITY_LOW, check_all_paths, p, NULL);
	// initialize the recognizers box with all counts zero
	// when check_all_paths finishes, it will update this
	init_recognizers(GTK_TREE_VIEW(WID("list_recognizers")));

	gtk_main();
	return 0;
}

static timeval limit_start, limit_end;

static void times_changed(float start_time, float end_time) {
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

static void init_times(void) {
	std::pair<timeval, timeval> times = pf->get_times();

	GtkAdjustment *end_time_adj = GTK_RANGE(WID("end_time"))->adjustment;
	GtkAdjustment *start_time_adj = GTK_RANGE(WID("start_time"))->adjustment;
	start_time_adj->upper = end_time_adj->value = end_time_adj->upper = (times.second - times.first)/1000000.0;
	gtk_adjustment_changed(start_time_adj);
	gtk_adjustment_changed(end_time_adj);
	printf("first time: %ld.%06ld    last time: %ld.%06ld\n",
		times.first.tv_sec, times.first.tv_usec,
		times.second.tv_sec, times.second.tv_usec);

	g_signal_connect(G_OBJECT(end_time_adj), "value_changed", (GCallback)end_time_changed, NULL);
	g_signal_connect(G_OBJECT(start_time_adj), "value_changed", (GCallback)start_time_changed, NULL);

	first_time = limit_start = times.first;
	limit_end = times.second;
}

static void init_tasks(GtkTreeView *tree) {
	list_tasks = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_tasks), T_COL_COUNT, GTK_SORT_DESCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_tasks));

	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Count", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, T_COL_COUNT);

	col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, T_COL_NAME);

	fill_tasks();
}

static void init_pools(GtkTreeView *tree) {
	list_pools = gtk_list_store_new(4, G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_pools), PO_COL_ID, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_pools));

	GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("", renderer, "active", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, PO_COL_THIS_PATH);

	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes("Pool", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, PO_COL_ID);

	col = gtk_tree_view_column_new_with_attributes("Hostname", renderer, "text", 2, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, PO_COL_HOST);

	col = gtk_tree_view_column_new_with_attributes("Count", renderer, "text", 3, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, PO_COL_COUNT);

	fill_pools();
}

static void init_paths(GtkTreeView *tree) {
	list_paths = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_paths), P_COL_ID, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_paths));

	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, P_COL_ID);

	col = gtk_tree_view_column_new_with_attributes("Path name", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, P_COL_NAME);

	fill_paths();
}

static void init_recognizers(GtkTreeView *tree) {
	list_recognizers = gtk_list_store_new(7,
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_recognizers), R_COL_NAME, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(list_recognizers));

	GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("", renderer, "active", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, R_COL_PATH_MATCH);

	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes("Recognizer", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, R_COL_NAME);

	col = gtk_tree_view_column_new_with_attributes("", renderer, "text", 2, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, R_COL_TYPE);

	col = gtk_tree_view_column_new_with_attributes("", renderer, "text", 3, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, R_COL_COMPLETE);

	col = gtk_tree_view_column_new_with_attributes("Paths", renderer, "text", 4, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, R_COL_MATCH_COUNT);

	col = gtk_tree_view_column_new_with_attributes("Resource violations", renderer, "text", 5, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, R_COL_RESOURCES);

	fill_recognizers();
}

static void set_statusbar(const char *fmt, ...) {
	if (!fmt) {
		gtk_label_set_text(GTK_LABEL(WID("status")), NULL);
		return;
	}
	char *str;
	va_list arg;
	va_start(arg, fmt);
	str = g_strdup_vprintf(fmt, arg);
	va_end(arg);
	assert(!strchr(fmt, '\n'));
	gtk_label_set_text(GTK_LABEL(WID("status")), str);
	g_free(str);
}

void fill_tasks(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("tasks_search")));
	gtk_list_store_clear(list_tasks);
	GtkTreeIter iter;
	set_statusbar("Reading tasks");
	std::vector<NameRec> tasks = pf->get_tasks(search);
	for (unsigned int i=0; i<tasks.size(); i++) {
		gtk_list_store_append(list_tasks, &iter);
		gtk_list_store_set(list_tasks, &iter,
			T_COL_COUNT,  tasks[i].count,
			T_COL_NAME,   tasks[i].name.c_str(),
			-1);
	}
	set_statusbar(NULL);
}

//static std::map<int, std::vector<PathThread *> > thread_pools;
void fill_pools(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("pools_search")));
	gtk_list_store_clear(list_pools);
	GtkTreeIter iter;
	std::vector<ThreadPoolRec> thread_pools = pf->get_thread_pools(search);
	for (unsigned int i=0; i<thread_pools.size(); i++) {
		gtk_list_store_append(list_pools, &iter);
		gtk_list_store_set(list_pools, &iter,
			PO_COL_THIS_PATH, FALSE,
			PO_COL_ID,        pf->find_thread_pool(StringInt(thread_pools[i].host, thread_pools[i].pid)),
			PO_COL_HOST,      thread_pools[i].host.c_str(),
			PO_COL_COUNT,     thread_pools[i].count,
			-1);
	}
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
static void add_path(int pathid, const std::string &name) {
	PathStub *ps = paths[pathid];
	if (!still_checking) {
		if (!ps) return;  // empty path
		assert(ps->path_id == pathid);
		if (ps->ts_end < limit_start || ps->ts_start > limit_end) return;
	}
	if (!should_show(ps) || shown_paths.size() == MAX_PATHS_DISPLAYED) return;
	GtkTreeIter iter;
	gtk_list_store_append(list_paths, &iter);
	gtk_list_store_set(list_paths, &iter,
		P_COL_ID,    pathid,
		P_COL_NAME,  name.c_str(),
		-1);
	shown_paths.push_back(pathid);
}

// !! should only re-query if the search string or recognizer filters have changed
void fill_paths(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("paths_search")));
	gtk_list_store_clear(list_paths);
	shown_paths.clear();
	set_statusbar("Reading paths");
	std::vector<NameRec> pathnames = pf->get_path_ids(search);
	for (unsigned int i=0; i<pathnames.size(); i++)
		add_path(pathnames[i].count, pathnames[i].name);
	set_statusbar(NULL);
}

static gboolean update_recognizer_count_each(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
	int rid;
	assert(model == GTK_TREE_MODEL(list_recognizers));
	gtk_tree_model_get(model, iter, R_COL_RID, &rid, -1);
	if (rid == RID_UNVALIDATED)
		gtk_list_store_set(list_recognizers, iter, R_COL_MATCH_COUNT, invalid_paths_count, -1);
	else
		gtk_list_store_set(list_recognizers, iter, R_COL_MATCH_COUNT, match_count[rid], R_COL_RESOURCES, resources_count[rid], -1);
	return FALSE;
}

void update_recognizer_counts(void) {
	gtk_tree_model_foreach(GTK_TREE_MODEL(list_recognizers), update_recognizer_count_each, NULL);
}

void fill_recognizers(void) {
	const char *search = gtk_entry_get_text(GTK_ENTRY(WID("recognizers_search")));
	gtk_list_store_clear(list_recognizers);
	GtkTreeIter iter;
	std::vector<RecognizerBase*>::const_iterator rp;
	for (unsigned int i=0; i<recognizers.size(); i++) {
		RecognizerBase *rp = recognizers[i];
		if (search && search[0]) {
			if (search[0] == '!') {
				if (rp->name.find(&search[1]) != std::string::npos) continue;
			}
			else {
				if (rp->name.find(search) == std::string::npos) continue;
			}
		}
		char pathtype[] = "X";      // V(alidator), I(nvalidator), or R(ecognizer)
		char pathcomplete[] = "X";  // C(omplete), F(ragment), or S(et)
		pathtype[0] = toupper(path_type_to_string(rp->pathtype)[0]);
		pathcomplete[0] = toupper(rp->type_string()[0]);
		gtk_list_store_append(list_recognizers, &iter);
		gtk_list_store_set(list_recognizers, &iter,
			R_COL_PATH_MATCH,   FALSE,
			R_COL_NAME,         rp->name.c_str(),
			R_COL_TYPE,         pathtype,
			R_COL_COMPLETE,     pathcomplete,
			R_COL_MATCH_COUNT,  match_count[i],
			R_COL_RESOURCES,    resources_count[i],
			R_COL_RID,          i,
			-1);
	}
	gtk_list_store_append(list_recognizers, &iter);
	gtk_list_store_set(list_recognizers, &iter,
		R_COL_NAME,         "(unvalidated)",
		R_COL_MATCH_COUNT,  invalid_paths_count,
		R_COL_RID,          RID_UNVALIDATED,
		-1);
}

#ifndef M_LOGE2
#define M_LOGE2 0.693147180559945
#endif
#define SLIDER_TO_ZOOM(z) pow(2, (z))
#define ZOOM_TO_SLIDER(z) (log(z)/M_LOGE2)
#define DEFAULT_ZOOM 0.6  /* slider value: 2.27 */
GtkWidget *create_dag(const char *wid) {
	GtkWidget *dag = gtk_dag_new();
	gtk_dag_set_zoom(GTK_DAG(dag), DEFAULT_ZOOM);
	gtk_widget_show(dag);

	return dag;
}

GtkWidget *create_comm_graph(const char *wid) {
#ifdef HAVE_RSVG
	GtkWidget *graph = gtk_graph_new();
	gtk_graph_set_zoom(GTK_GRAPH(graph), DEFAULT_ZOOM);
#else
	GtkWidget *graph = gtk_label_new("No graph functionality: rsvg not found");
#endif
	gtk_widget_show(graph);
	return graph;
}

GtkWidget *create_plot(const char *wid) {
	GtkWidget *plot = gtk_plot_new((GtkPlotFlags)(PLOT_DEFAULTS|PLOT_Y0));
	gtk_widget_show(plot);
	return plot;
}

GtkWidget *create_pathtl(const char *wid) {
	GtkWidget *pathtl = gtk_pathtl_new(first_time);
	gtk_widget_show(pathtl);
	return pathtl;
}

gchar *zoom_format_value(GtkScale *scale, gdouble value) {
	return g_strdup_printf("%.2f", SLIDER_TO_ZOOM(value));
}

gchar *zoom_format_value_int(GtkScale *scale, gdouble value) {
	return g_strdup_printf("%d", (int)SLIDER_TO_ZOOM(value));
}

gchar *zoom_format_value_pathtl(GtkScale *scale, gdouble value) {
	int val = (int)SLIDER_TO_ZOOM(value);
	if (val >= 10000000)
		return g_strdup_printf("%7d/us", val/1000000);
	if (val >= 1000000)
		return g_strdup_printf("%.1f/us", val/1000000.0);
	if (val >= 10000)
		return g_strdup_printf("%d/ms", val/1000);
	if (val >= 1000)
		return g_strdup_printf("%.1f/ms", val/1000.0);
	return g_strdup_printf("%d/sec", val);
}

void dag_zoom_value_changed(GtkRange *range) {
	gtk_dag_set_zoom(GTK_DAG(WID("dag")),
		SLIDER_TO_ZOOM(gtk_range_get_value(range)));
}

void pathtl_zoom_value_changed(GtkRange *range) {
	gtk_pathtl_set_zoom(GTK_PATHTL(WID("pathtl")),
		SLIDER_TO_ZOOM(gtk_range_get_value(range))/1000000.0);
}

void comm_zoom_value_changed(GtkRange *range) {
#ifdef HAVE_RSVG
	gtk_graph_set_zoom(GTK_GRAPH(WID("comm_graph")),
		SLIDER_TO_ZOOM(gtk_range_get_value(range)));
#endif
}

void pathtl_zoom_changed(GtkPathTL *pathtl, double zoom) {
	gtk_range_set_value(GTK_RANGE(WID("pathtl_zoom")), ZOOM_TO_SLIDER(1000000*zoom));
}

static GtkTreeStore *popup_events;
static void popup_append_event(const PathEvent *ev, GtkTreeIter *iter, GtkTreeIter *parent) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%ld.%06ld", ev->start().tv_sec, ev->start().tv_usec);
	std::string txt = ev->to_string();
	gtk_tree_store_append(popup_events, iter, parent);
	gtk_tree_store_set(popup_events, iter,
		DP_COL_TIME,    buf,
		DP_COL_NAME,    txt.c_str(),
		DP_COL_EVENT,   ev,
		-1);
}

// returns true if the given recv 'match' is somewhere in 'list' or its
// children
static bool popup_match_recv(const PathEventList &list, PathMessageRecv *match) {
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				if (popup_match_recv(dynamic_cast<PathTask*>(list[i])->children, match)) return true;
				break;
			case PEV_MESSAGE_RECV:
				if (list[i] == match) return true;
				break;
			default: ;
		}
	}
	return false;
}

static void popup_fill_events(const PathEvent *pev, GtkTreeIter *parent) {
	GtkTreeIter iter;
	popup_append_event(pev, &iter, parent);
	if (pev->type() == PEV_TASK) {
		const PathEventList &list = dynamic_cast<const PathTask*>(pev)->children;
		for (unsigned int i=0; i<list.size(); i++)
			popup_fill_events(list[i], parent);
	}
}

static bool popup_fill_events(const PathEventList &list, PathMessageRecv *match, bool *found_start, GtkTreeIter *parent) {
	GtkTreeIter iter;
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				// this task is important if we've already gotten the recv, _or_
				// if the recv is one of our descendants
				if (*found_start || popup_match_recv(dynamic_cast<PathTask*>(list[i])->children, match))
					popup_append_event(list[i], &iter, parent);
				if (popup_fill_events(dynamic_cast<PathTask*>(list[i])->children, match, found_start, &iter))
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

void pathtl_node_clicked(GtkPathTL *pathtl, const PathEvent *pev) {
	if (!pev) {
		gtk_label_set_text(GTK_LABEL(WID("pathtl_label_host")), "");
		gtk_label_set_text(GTK_LABEL(WID("pathtl_label_task")), "");
		return;
	}
	gtk_label_set_text(GTK_LABEL(WID("pathtl_label_host")), threads[pev->thread_id]->host.c_str());
	switch (pev->type()) {
		case PEV_TASK:
			gtk_label_set_text(GTK_LABEL(WID("pathtl_label_task")), dynamic_cast<const PathTask*>(pev)->name);
			break;
		case PEV_NOTICE:
			gtk_label_set_text(GTK_LABEL(WID("pathtl_label_task")), dynamic_cast<const PathNotice*>(pev)->name);
			break;
		default: ;
	}

	if (dagpopup_xml && (GTK_OBJECT_FLAGS(WID_D("pathview_dagpopup")) & GTK_VISIBLE))
		pathtl_node_activated(pathtl, pev);
}

static void dagpopup_init(int thread, const char *host, const char *prog) {
	if (!dagpopup_xml) {
		dagpopup_xml = glade_xml_new(glade_file, "pathview_dagpopup", NULL);
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
	else
		gtk_window_present(GTK_WINDOW(WID_D("pathview_dagpopup")));

	gtk_label_set_text(GTK_LABEL(WID_D("label_thread")), itoa(thread));
	gtk_label_set_text(GTK_LABEL(WID_D("label_host")), host);
	gtk_label_set_text(GTK_LABEL(WID_D("label_program")), prog);

	gtk_tree_store_clear(popup_events);
}

void pathtl_node_activated(GtkPathTL *pathtl, const PathEvent *pev) {
	const PathTask *task = dynamic_cast<const PathTask*>(pev);
	dagpopup_init(task->thread_id,
		threads[task->thread_id]->host.c_str(), threads[task->thread_id]->prog.c_str());

	popup_fill_events(pev, NULL);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(WID_D("dagpopup_list")));
}

void max_points_value_changed(GtkRange *range) {
	max_graph_points = (int)SLIDER_TO_ZOOM(gtk_range_get_value(range));
	regraph();
}

struct ForEachPathStub {
	const PathStub *ps;
	bool found;
};
static gboolean find_and_select(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
	int pathid;
	gtk_tree_model_get(model, iter, P_COL_ID, &pathid, -1);
	if (pathid == ((ForEachPathStub*)data)->ps->path_id) {
		GtkTreeView *tv = GTK_TREE_VIEW(WID("list_paths"));
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(tv), iter);
		gtk_tree_view_scroll_to_cell(tv, path, NULL, TRUE, 0.5, 0.5);
		paths_activate_row(tv, path, NULL);
		((ForEachPathStub*)data)->found = true;
		return TRUE;
	}
	return FALSE;
}

void plot_point_clicked(GtkPlot *plot, GtkPlotPoint *point) {
	char buf[64];
	if (point) {
		sprintf(buf, "(%.3f, %.3f)", point->x, point->y);
		gtk_label_set_text(GTK_LABEL(WID("graph_pos")), buf);

		if (point->user_data) {
			PathStub *ps = (PathStub*)point->user_data;
			gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_paths"))));
			ForEachPathStub data = { ps, false };

			gtk_tree_model_foreach(GTK_TREE_MODEL(list_paths), find_and_select, &data);
			if (data.found)
				set_statusbar("Path %d activated", ps->path_id);
			else
				set_statusbar("Path %d clicked but not visible", ps->path_id);
		}
		else {   // clicked a point with no data
			gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_paths"))));
			set_statusbar(NULL);
		}
	}
	else {     // clicked far away from any point
		gtk_label_set_text(GTK_LABEL(WID("graph_pos")), "(X, Y)");
		gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_paths"))));
		set_statusbar(NULL);
	}
}

void dag_node_clicked(GtkDAG *dag, DAGNode *node) {
	PathMessageRecv *pmr = (PathMessageRecv*)node->user_data;
	int thread_id = pmr ? pmr->thread_id : Path::get_thread_id(active_path->thread_pools[active_path->root_thread]);
	gtk_label_set_text(GTK_LABEL(WID("dag_label_host")), threads[thread_id]->host.c_str());
	gtk_label_set_text(GTK_LABEL(WID("dag_label_program")), threads[thread_id]->prog.c_str());

	if (dagpopup_xml && (GTK_OBJECT_FLAGS(WID_D("pathview_dagpopup")) & GTK_VISIBLE))
		dag_node_activated(dag, node);
}

void dag_node_activated(GtkDAG *dag, DAGNode *node) {
	PathMessageRecv *pmr = (PathMessageRecv*)node->user_data;
	int thread_id = pmr ? pmr->thread_id : Path::get_thread_id(active_path->thread_pools[active_path->root_thread]);
	dagpopup_init(thread_id,
		gtk_label_get_text(GTK_LABEL(WID("dag_label_host"))),
		gtk_label_get_text(GTK_LABEL(WID("dag_label_program"))));

	bool found_start = pmr == NULL;
	popup_fill_events(active_path->get_events(thread_id), pmr, &found_start, NULL);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(WID_D("dagpopup_list")));
}

void each_recognizer(GtkTreeModel *ign1, GtkTreePath *ign2, GtkTreeIter *iter,
		gpointer data) {
	int rid;
	gtk_tree_model_get(GTK_TREE_MODEL(list_recognizers), iter, R_COL_RID, &rid, -1);
	if (rid == RID_UNVALIDATED) rid = recognizers.size();
	recognizers_filter->set(rid, true);
	(*static_cast<int*>(data))++;
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

void each_recognizer_mismatch(GtkTreeModel *ign1, GtkTreePath *ign2, GtkTreeIter *iter,
		gpointer data) {
	char *name;
	int rid;
	gtk_tree_model_get(GTK_TREE_MODEL(list_recognizers), iter, R_COL_NAME, &name, R_COL_RID, &rid, -1);
	if (rid == RID_UNVALIDATED) { g_free(name); return; }   // can't do mismatch details for "unvalidated"
	if (recognizers[rid]->type_string()[0] == 's') return; // can't do it for set recognizers, either

	debug_failed_matches = true;
	bool resources;
	if (!recognizers[rid]->check(active_path, &resources, NULL)) {
		GtkWidget *win, *label;
		win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_container_set_border_width(GTK_CONTAINER(win), 8);
		char *title = g_strdup_printf("Mismatch details: %s", name);
		gtk_window_set_title(GTK_WINDOW(win), title);
		g_free(title);
		if (!debug_buffer.empty())
			debug_buffer.erase(debug_buffer.length()-1);  // chop off the trailing newline
		label = gtk_label_new(debug_buffer.c_str());
		gtk_container_add(GTK_CONTAINER(win), label);
		gtk_widget_show_all(win);
	}
	debug_failed_matches = false;

	g_free(name);
}

void debug_mismatch(void) {
	if (!active_path) return;
	GtkTreeSelection *sel =
		gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_recognizers")));
	gtk_tree_selection_selected_foreach(sel, each_recognizer_mismatch, NULL);
}

static void tasks_plot_row(GtkTreeModel *model, GtkTreePath *ign2, GtkTreeIter *iter, gpointer _plot) {
	assert(model == GTK_TREE_MODEL(list_tasks));
	assert(iter != NULL);

	GraphQuantity graph_quantity = (GraphQuantity)gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_quantity")));
	GraphStyle style = (GraphStyle)gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_style")));

	const char *taskname;
	gtk_tree_model_get(GTK_TREE_MODEL(list_tasks), iter, T_COL_NAME, &taskname, -1);

	std::vector<GraphPoint> data = pf->get_task_metric(taskname, graph_quantity, style, max_graph_points);
	if (data.size() == 0) return;

	GtkPlot *plot = GTK_PLOT(_plot);
	gtk_plot_set_key(plot, style == STYLE_CDF ? PLOT_KEY_LR : PLOT_KEY_UR);
	gtk_plot_start_new_line(plot, taskname);

	// !! change PathFactory to provide pathid for each data point?
	for (unsigned int i=0; i<data.size(); i++)
		gtk_plot_add_point(plot, data[i].x, data[i].y, data[i].pathid == -1 ? NULL : paths[data[i].pathid]);
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

void tasks_activated(void) {
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

static void paths_plot_recognizer(GtkTreeModel *model, GtkTreePath *ign2, GtkTreeIter *iter,
		gpointer data) {
	if (model) {
		assert(model == GTK_TREE_MODEL(list_recognizers));
		assert(iter != NULL);
	}

	GraphQuantity graph_quantity = (GraphQuantity)gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_quantity")));
	GraphStyle style = (GraphStyle)gtk_combo_box_get_active(GTK_COMBO_BOX(WID("graph_style")));

	GtkPlot *plot = GTK_PLOT(data);
	gtk_plot_set_key(plot, style == STYLE_CDF ? PLOT_KEY_LR : PLOT_KEY_UR);
	const char *label = NULL;
	int rid;
	if (model)
		gtk_tree_model_get(GTK_TREE_MODEL(list_recognizers), iter, R_COL_RID, &rid, R_COL_NAME, &label, -1);

	gtk_plot_start_new_line(plot, label);

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
		PathStub *ps = paths[shown_paths[i]];
		if (!ps) continue;  // path has not been read+checked yet
		if (!ps->valid) continue;
		if (model) {
			if (rid == RID_UNVALIDATED) {
				if (ps->validated) continue;
			}
			else {
				if (!ps->recognizers[rid]) continue;
			}
		}

		float val = 0.0;
		switch (graph_quantity) {
			case QUANT_START:     val = (ps->ts_start - first_time) / 1000000.0;   break;
			case QUANT_REAL:      val = (ps->ts_end - ps->ts_start) / 1000.0;      break;
			case QUANT_CPU:       val = (ps->stime + ps->utime) / 1000.0;          break;
			case QUANT_UTIME:     val = ps->utime / 1000.0;                        break;
			case QUANT_STIME:     val = ps->stime / 1000.0;                        break;
			case QUANT_MAJFLT:    val = ps->major_fault;                           break;
			case QUANT_MINFLT:    val = ps->minor_fault;                           break;
			case QUANT_VCS:       val = ps->vol_cs;                                break;
			case QUANT_IVCS:      val = ps->invol_cs;                              break;
			case QUANT_MESSAGES:  val = ps->messages;                              break;
			case QUANT_DEPTH:     val = ps->depth;                                 break;
			case QUANT_HOSTS:     val = ps->hosts;                                 break;
			case QUANT_BYTES:     val = ps->bytes;                                 break;
			case QUANT_THREADS:   val = ps->threads;                               break;
			case QUANT_LATENCY:   val = ps->latency / 1000.0;                      break;
			default: assert(!"invalid quant");
		}
		assert(val >= 0);
		switch (style) {
			case STYLE_CDF:
				points.f[npoints].x = val;
				points.f[npoints].ps = ps;
				break;
			case STYLE_PDF:
				points.i[npoints] = (int)(val + 0.5);
				break;
			case STYLE_TIME:
				points.pp[npoints].x = (ps->ts_start - first_time)/1000000.0;
				points.pp[npoints].y = val;
				points.pp[npoints].ps = ps;
				break;
		}
		npoints++;
	}
	if (npoints == 0) return;  // nothing to plot
	if (style == STYLE_CDF && npoints == 1) return;  // don't plot invalid CDF
	unsigned int j, last_x = 1<<30;
	int skip = npoints / (max_graph_points - 1) + 1;
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
}

void paths_graph(void) {
	graph_common("Performance: Paths", GRAPH_PATHS);

	GtkPlot *plot = GTK_PLOT(WID("plot"));

	if (recognizers_filter) {
		GtkTreeSelection *sel =
			gtk_tree_view_get_selection(GTK_TREE_VIEW(WID("list_recognizers")));
		gtk_tree_selection_selected_foreach(sel, paths_plot_recognizer, plot);
	}
	else
		paths_plot_recognizer(NULL, NULL, NULL, plot);  // plot all at once

	gtk_plot_thaw(plot);
	if (plot->lines->len > 0)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(WID("notebook")), NOTEBOOK_GRAPH);
}

void pools_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
}

static gboolean set_recognizer_if_matched(GtkTreeModel *model, GtkTreePath *ign, GtkTreeIter *iter, gpointer data) {
	assert(model == GTK_TREE_MODEL(list_recognizers));
	PathStub *ps = (PathStub*)data;
	if (ps) {
		int rid;
		gtk_tree_model_get(model, iter, R_COL_RID, &rid, -1);
		bool matched;
		if (rid == RID_UNVALIDATED)
			matched = !ps->validated;
		else
			matched = ps->recognizers[rid];
		gtk_list_store_set(list_recognizers, iter, R_COL_PATH_MATCH, matched, -1);
	}
	else
		gtk_list_store_set(list_recognizers, iter, R_COL_PATH_MATCH, FALSE, -1);
	
	return FALSE;
}

static gboolean set_pool_if_matched(GtkTreeModel *model, GtkTreePath *ign, GtkTreeIter *iter, gpointer data) {
	assert(model == GTK_TREE_MODEL(list_pools));
	Path *path = (Path*)data;
	if (path) {
		int pool_id;
		gtk_tree_model_get(model, iter, PO_COL_ID, &pool_id, -1);
		bool matched = path->thread_pools.find(pool_id) != path->thread_pools.end();
		gtk_list_store_set(list_pools, iter, PO_COL_THIS_PATH, matched, -1);
	}
	else
		gtk_list_store_set(list_pools, iter, PO_COL_THIS_PATH, FALSE, -1);
	
	return FALSE;
}

void paths_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	GtkTreeIter iter;
	int pathid;
	char *pathname;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(list_paths), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(list_paths), &iter, P_COL_ID, &pathid, P_COL_NAME, &pathname, -1);
	PathStub *ps = paths[pathid];

	// call it even if ps==NULL -- it will just set to false
	int old_sort_column;
	GtkSortType old_sort_order;
	gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(list_recognizers), &old_sort_column, &old_sort_order);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_recognizers),
		GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, old_sort_order);
	gtk_tree_model_foreach(GTK_TREE_MODEL(list_recognizers), set_recognizer_if_matched, ps);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_recognizers), old_sort_column, old_sort_order);

	if (active_path) delete active_path;
	active_path = pf->get_path(pathid);

	gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(list_pools), &old_sort_column, &old_sort_order);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_pools),
		GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, old_sort_order);
	gtk_tree_model_foreach(GTK_TREE_MODEL(list_pools), set_pool_if_matched, active_path);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_pools), old_sort_column, old_sort_order);

	if (!active_path->valid()) {
		set_statusbar("Path %s (%d) is malformed", pathname, pathid);
		g_free(pathname);
		delete active_path;
		active_path = NULL;
		return;
	}
	char *buf = g_strdup_printf("%s (%d)", pathname, pathid);
	g_free(pathname);
	gtk_label_set_text(GTK_LABEL(WID("dag_label_path")), buf);
	gtk_label_set_text(GTK_LABEL(WID("pathtl_label_path")), buf);
	g_free(buf);
	gtk_pathtl_set(GTK_PATHTL(WID("pathtl")), active_path);
	//printf("new pathtl zoom is %f\n", GTK_PATHTL(WID("pathtl"))->zoom*1000000.0);
	//!! put it in the slider!
	fill_dag(GTK_DAG(WID("dag")), active_path);
#ifdef HAVE_RSVG
	fill_comm(GTK_GRAPH(WID("comm_graph")), active_path);
#endif
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

void on_comm_layout_changed(GtkComboBox *cb) {
#ifdef HAVE_RSVG
	static const char *cmd[] = { "dot", "neato", "circo", "twopi", "fdp" };
	int idx = gtk_combo_box_get_active(cb);
	assert(idx >= 0);
	assert((unsigned int)idx < sizeof(cmd)/sizeof(cmd[0]));
	gtk_graph_set_layout_program(GTK_GRAPH(WID("comm_graph")), cmd[idx]);
#endif
}

void pathtl_flags_changed(void) {
	GtkPathtlFlags flags = (GtkPathtlFlags)0;
	GtkPathTL *pathtl = GTK_PATHTL(WID("pathtl"));
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(WID("pathtl_show_subtasks")))) flags = (GtkPathtlFlags)(flags | PATHTL_SHOW_SUBTASKS);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(WID("pathtl_show_notices")))) flags = (GtkPathtlFlags)(flags | PATHTL_SHOW_NOTICES);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(WID("pathtl_show_messages")))) flags = (GtkPathtlFlags)(flags | PATHTL_SHOW_MESSAGES);
	gtk_pathtl_set_flags(pathtl, flags);
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
	dag_nodes[pmr] = gtk_dag_add_node(dag, itoa(threads[pmr->thread_id]->pool), parent, NULL, 0, (void*)pmr);
	return dag_nodes[pmr];
}

// returns true if the end of interval (a second recv) is found
static bool fill_dag_from_list(GtkDAG *dag, const PathEventList &list, const Path *path,
		const PathMessageRecv *match, bool *found_start) {
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				if (fill_dag_from_list(dag, dynamic_cast<PathTask*>(list[i])->children, path, match, found_start))
					return true;
				break;
			case PEV_MESSAGE_RECV:
				if (*found_start) return true;
				if (list[i] == match) *found_start = true;
				break;
			case PEV_MESSAGE_SEND:
				if (*found_start) {
					bool child_start = false;
					const PathMessageSend *pms = dynamic_cast<PathMessageSend*>(list[i]);
					fill_dag_one_message(dag, pms->recv, path);
					// don't care if receiver of this message ends his interval
					if (pms->recv)
						(void)fill_dag_from_list(dag, path->get_events(pms->recv->thread_id), path,
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
	fill_dag_from_list(dag, path->thread_pools.find(path->root_thread)->second, path, NULL, &found_start);
	gtk_dag_thaw(dag);
}

static void fill_comm_from_list(const PathEventList &list,
		std::set<std::pair<std::string, std::string> > *host_pairs) {
	for (unsigned int i=0; i<list.size(); i++) {
		PathMessageSend *pms;
		switch (list[i]->type()) {
			case PEV_TASK:
				fill_comm_from_list(dynamic_cast<PathTask*>(list[i])->children, host_pairs);
				break;
			case PEV_MESSAGE_SEND:
				pms = dynamic_cast<PathMessageSend*>(list[i]);
				if (pms->recv)
					host_pairs->insert(std::pair<std::string, std::string>(
						threads[pms->thread_id]->host, threads[pms->recv->thread_id]->host));
				break;
			default:;
		}
	}
}

#ifdef HAVE_RSVG
static void fill_comm(GtkGraph *graph, const Path *path) {
	std::set<std::pair<std::string, std::string> > host_pairs;
	std::map<int,PathEventList>::const_iterator thread;
	for (thread=path->thread_pools.begin(); thread!=path->thread_pools.end(); thread++)
		fill_comm_from_list(thread->second, &host_pairs);

	std::map<std::string, GtkGraphNode*> nodes;
	gtk_graph_freeze(graph);
	gtk_graph_clear(graph);
	for (std::set<std::pair<std::string, std::string> >::const_iterator hostp = host_pairs.begin();
			hostp != host_pairs.end();
			hostp++) {
		if (nodes.find(hostp->first) == nodes.end()) {
			nodes[hostp->first] = gtk_graph_add_node(graph, hostp->first.c_str());
		}
		if (nodes.find(hostp->second) == nodes.end()) {
			nodes[hostp->second] = gtk_graph_add_node(graph, hostp->second.c_str());
		}

		(void)gtk_graph_add_edge(graph, nodes[hostp->first], nodes[hostp->second], TRUE);
	}
	gtk_graph_simplify(graph);
	gtk_graph_thaw(graph);
}
#endif  // HAVE_RSVG

static gboolean check_all_paths(void *iter) {
	std::vector<int>::const_iterator *p = (std::vector<int>::const_iterator*)iter;
	if (*p == path_ids.end()) {
		set_statusbar("Done checking paths");
		delete p;
		fill_paths();
		update_recognizer_counts();
		still_checking = false;
		return false;
	}
	check_path(**p);
	++(*p);
	return true;
}

static void check_path(int pathid) {
	unsigned int i;
	Path *path = pf->get_path(pathid);
	PathStub *ps = paths[pathid] = new PathStub(*path, recognizers.size()+1);

	if (!path->valid()) {
		set_statusbar("Path %d malformed -- not checked", pathid);
		//malformed_paths_count++;
		delete path;
		return;
	}
	int validators_matched = 0;
	std::map<std::string, bool> match_map;
	for (i=0; i<recognizers.size(); i++) {
		RecognizerBase *rp = recognizers[i];
		bool resources = true;
		if (rp->check(path, &resources, &match_map)) {
			if (resources) {
				ps->recognizers.set(i, true);
				match_count[i]++;
				if (rp->pathtype == VALIDATOR) {
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
	for (i=0; i<recognizers.size(); i++)
		if (recognizers[i]->pathtype == INVALIDATOR && ps->recognizers[i]) {
			ps->validated = false;
			// This may be misleading because some validators MAY have been
			// matched.  It's OK, because the only thing this variable is used
			// for is incrementing invalid_paths_count.
			validators_matched = 0;
			break;
		}
	if (validators_matched == 0) invalid_paths_count++;

	// update match_count, resources_count, and invalid_paths_count once per second
	static int last_time = time(0);
	int now = time(0);
	if (now != last_time) {
		update_recognizer_counts();
		last_time = now;
	}

	delete path;
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
		TP_COL_NAME,   label,
		TP_COL_VALUE,  buf,
		-1);
	va_end(args);
}
void on_dagpopup_row_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	GtkTreeIter iter;
	PathEvent *ev;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(popup_events), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(popup_events), &iter, DP_COL_EVENT, &ev, -1);
	assert(ev);

	if (!taskpopup_xml) {
		taskpopup_xml = glade_xml_new(glade_file, "pathview_taskpopup", NULL);
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
		gtk_window_present(GTK_WINDOW(WID_T("pathview_taskpopup")));
		gtk_list_store_clear(taskpopup_props);
	}

	switch (ev->type()) {
		case PEV_TASK:{
			const PathTask *task = dynamic_cast<const PathTask*>(ev);
			taskpopup_add_item("Name", "%s", task->name);
			taskpopup_add_item("Start time", "%ld.%06ld", task->ts.tv_sec, task->ts.tv_usec);
			taskpopup_add_item("End time", "%ld.%06ld", task->ts_end.tv_sec, task->ts_end.tv_usec);
			taskpopup_add_item("Real time", "%.3f ms", task->tdiff/1000.0);
			taskpopup_add_item("System time", "%.3f ms", task->stime/1000.0);
			taskpopup_add_item("User time", "%.3f ms", task->utime/1000.0);
			taskpopup_add_item("Busy %", "%.3f", 100.0*(task->utime+task->stime)/task->tdiff);
			taskpopup_add_item("Major faults", "%d", task->major_fault);
			taskpopup_add_item("Minor faults", "%d", task->minor_fault);
			taskpopup_add_item("Voluntary context switches", "%d", task->vol_cs);
			taskpopup_add_item("Involuntary context switches", "%d", task->invol_cs);
			taskpopup_add_item("Starting thread", "%d", task->thread_id);
			}
			break;
		case PEV_NOTICE:{
			const PathNotice *notice = dynamic_cast<const PathNotice*>(ev);
			taskpopup_add_item("Name", "%s", notice->name);
			taskpopup_add_item("Time", "%ld.%06ld", notice->ts.tv_sec, notice->ts.tv_usec);
			}
			break;
		case PEV_MESSAGE_SEND:{
			const PathMessageSend *pms = dynamic_cast<const PathMessageSend*>(ev);
			int latency = pms->recv->ts - pms->ts;
			taskpopup_add_item("Send time", "%ld.%06ld", pms->ts.tv_sec, pms->ts.tv_usec);
			taskpopup_add_item("Receive time", "%ld.%06ld", pms->recv->ts.tv_sec, pms->recv->ts.tv_usec);
			taskpopup_add_item("Latency", "%.6f sec", latency/1000000.0);
			taskpopup_add_item("Size", "%d bytes", pms->size);
			}
			break;
		case PEV_MESSAGE_RECV:{
			const PathMessageRecv *pmr = dynamic_cast<const PathMessageRecv*>(ev);
			int latency = pmr->ts - pmr->send->ts;
			taskpopup_add_item("Send time", "%ld.%06ld", pmr->send->ts.tv_sec, pmr->send->ts.tv_usec);
			taskpopup_add_item("Receive time", "%ld.%06ld", pmr->ts.tv_sec, pmr->ts.tv_usec);
			taskpopup_add_item("Latency", "%.6f sec", latency/1000000.0);
			taskpopup_add_item("Size", "%d bytes", pmr->send->size);
			}
			break;
		default:
			assert(!"invalid event type");
	}

	// !! want to resize here, but can't do it with set_usize or
	// queue_resize.  What, then?
}

static const char *find_glade_file(void) {
	static const char *where_to_look[] = {
		"/usr/share/pip/pathview.glade",
		"/usr/local/share/pip/pathview.glade",
		"pathview.glade",
		BUILD_DIR"/pathview.glade",
		0
	};

	for (int i=0; where_to_look[i]; i++)
		if (access(where_to_look[i], R_OK) == 0)
			return where_to_look[i];
	return NULL;
}
