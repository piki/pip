/* Show a graph of all communications */
/* Find beginning and end times */
/* Clock synch */
/* Click on a path, show that path in the upper left */
/* Click on a task, show CDF of start times or CDF of any resource */
/* Show CDF of any resource by task, host, path, or recognizer */
/* click in the dag, highlight all other nodes with the same hostname, same thread */

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include "dag.h"
#include "graph.h"
#include "plot.h"
#include "path.h"
#include "pathtl.h"
#include "workqueue.h"

#define WID(name) glade_xml_get_widget(xml, name)
static GladeXML *xml;
static char *table_base;
MYSQL mysql;
static GtkListStore *tasks, *hosts, *paths, *recognizers;
static int status_bar_context;

extern Path *read_path(const char *base, int pathid);
static void init_tasks(GtkTreeView *tree);
static void init_hosts(GtkTreeView *tree);
static void init_paths(GtkTreeView *tree);
static void init_recognizers(GtkTreeView *tree);
static void fill_tasks(const char *search);
static void fill_hosts(const char *search);
static void fill_paths(const char *search);
static void fill_recognizers(const char *search);
static void fill_dag(GtkDAG *dag, const Path *path);

extern "C" {
GtkWidget *create_dag(const char *wid);
GtkWidget *create_comm_graph(const char *wid);
GtkWidget *create_plot(const char *wid);
GtkWidget *create_pathtl(const char *wid);
gchar *zoom_format_value(GtkScale *scale, gdouble value);
void zoom_value_changed(GtkRange *range);
void plot_point_clicked(GtkPlot *plot, GtkPlotPoint *point);
void dag_node_clicked(GtkDAG *dag, DAGNode *node);
void tasks_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void hosts_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void paths_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void recognizers_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc);
void tasks_search(GtkEntry *entry);
void hosts_search(GtkEntry *entry);
void paths_search(GtkEntry *entry);
void recognizers_search(GtkEntry *entry);
void on_graph_quantity_changed(GtkComboBox *cb);
void on_graph_style_changed(GtkComboBox *cb);
void on_aggregation_changed(GtkComboBox *cb);
void on_scale_times_toggled(GtkToggleButton *cb);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	if (argc != 2) {
		fprintf(stderr, "Usage:\n  %s table-base\n", argv[0]);
		return 1;
	}
	table_base = argv[1];
  glade_init();
  xml = glade_xml_new("pathview.glade", NULL, NULL);
  glade_xml_signal_autoconnect(xml);
  //gtk_object_unref(GTK_OBJECT(xml));
	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("graph_quantity")), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("graph_style")), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(WID("aggregation")), 0);
	status_bar_context = gtk_statusbar_get_context_id(GTK_STATUSBAR(WID("statusbar")), "foo");

	init_tasks(GTK_TREE_VIEW(WID("list_tasks")));
	init_hosts(GTK_TREE_VIEW(WID("list_hosts")));
	init_paths(GTK_TREE_VIEW(WID("list_paths")));
	init_recognizers(GTK_TREE_VIEW(WID("list_recognizers")));
  gtk_main();
  return 0;
}

static void init_tasks(GtkTreeView *tree) {
	tasks = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tasks), 1, GTK_SORT_DESCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(tasks));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Count", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_tasks(NULL);
}

static void init_hosts(GtkTreeView *tree) {
	hosts = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(hosts), 0, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(hosts));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Host", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Threads", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_hosts(NULL);
}

static void init_paths(GtkTreeView *tree) {
	paths = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(paths), 0, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(paths));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Path ID", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Path name", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_paths(NULL);
}

static void init_recognizers(GtkTreeView *tree) {
	recognizers = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(recognizers), 0, GTK_SORT_ASCENDING);
	gtk_tree_view_set_model(tree, GTK_TREE_MODEL(recognizers));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Recognizer", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 0);
	col = gtk_tree_view_column_new_with_attributes("Count", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(tree, col);
	gtk_tree_view_column_set_sort_column_id(col, 1);
	fill_recognizers(NULL);
}

static void add_task(MYSQL_ROW row, void *ign) {
	GtkTreeIter iter;
	gtk_list_store_append(tasks, &iter);
	gtk_list_store_set(tasks, &iter,
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

static void fill_tasks(const char *search) {
	gtk_list_store_clear(tasks);
	char *query;
	if (search && search[0])
		query = g_strdup_printf("SELECT name,COUNT(name) FROM %s_tasks WHERE name LIKE '%%%s%%' GROUP BY name LIMIT 500", table_base, search);
	else
		query = g_strdup_printf("SELECT name,COUNT(name) FROM %s_tasks GROUP BY name LIMIT 500", table_base);
	add_db_idler(query, add_task, first_task, last_task, NULL);
	g_free(query);
}

static void add_host(MYSQL_ROW row, void *ign) {
	GtkTreeIter iter;
	gtk_list_store_append(hosts, &iter);
	gtk_list_store_set(hosts, &iter,
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

static void fill_hosts(const char *search) {
	gtk_list_store_clear(hosts);
	char *query;
	if (search && search[0])
		query = g_strdup_printf("SELECT host,COUNT(host) FROM %s_threads WHERE host LIKE '%%%s%%' GROUP BY host", table_base, search);
	else
		query = g_strdup_printf("SELECT host,COUNT(host) FROM %s_threads GROUP BY host", table_base);
	add_db_idler(query, add_host, first_host, last_host, NULL);
	g_free(query);
}

static void add_path(MYSQL_ROW row, void *ign) {
	GtkTreeIter iter;
	gtk_list_store_append(paths, &iter);
	gtk_list_store_set(paths, &iter,
		0, atoi(row[0]),
		1, row[1],
		-1);
}

static void first_path(void *ign) {
	gtk_statusbar_push(GTK_STATUSBAR(WID("statusbar")), status_bar_context, "Reading paths");
}

static void last_path(void *ign) {
	gtk_statusbar_pop(GTK_STATUSBAR(WID("statusbar")), status_bar_context);
}

static void fill_paths(const char *search) {
	gtk_list_store_clear(paths);
	char *query;
	if (search && search[0])
		query = g_strdup_printf("SELECT pathid,pathblob FROM %s_paths WHERE pathblob LIKE '%%%s%%' LIMIT 1000", table_base, search);
	else
		query = g_strdup_printf("SELECT pathid,pathblob FROM %s_paths LIMIT 1000", table_base);
	add_db_idler(query, add_path, first_path, last_path, NULL);
	g_free(query);
}

static void fill_recognizers(const char *search) {
	gtk_list_store_clear(recognizers);
}

#define SLIDER_TO_ZOOM(z) pow(2, (z)-3)
#define DEFAULT_ZOOM 0.6  /* slider value: 2.27 */
GtkWidget *create_dag(const char *wid) {
	GtkWidget *ret = gtk_dag_new();
	gtk_dag_set_zoom(GTK_DAG(ret), DEFAULT_ZOOM);
	gtk_widget_show(ret);

	return ret;
}

GtkWidget *create_comm_graph(const char *wid) {
	GtkWidget *graph = gtk_graph_new();
	gtk_graph_freeze(GTK_GRAPH(graph));
	GtkGraphNode *a = gtk_graph_add_node(GTK_GRAPH(graph), "a");
	GtkGraphNode *b = gtk_graph_add_node(GTK_GRAPH(graph), "b");
	GtkGraphNode *c = gtk_graph_add_node(GTK_GRAPH(graph), "c");
	gtk_graph_add_edge(GTK_GRAPH(graph), a, b, TRUE);
	gtk_graph_add_edge(GTK_GRAPH(graph), a, c, FALSE);
	gtk_graph_add_edge(GTK_GRAPH(graph), b, c, FALSE);
	gtk_graph_thaw(GTK_GRAPH(graph));
	gtk_widget_show(graph);
	return graph;
}

GtkWidget *create_plot(const char *wid) {
	GtkWidget *ret = gtk_plot_new(PLOT_DEFAULTS);
	gtk_plot_start_new_line(GTK_PLOT(ret));
	gtk_plot_add_point(GTK_PLOT(ret), 1, 1);
	gtk_plot_add_point(GTK_PLOT(ret), 3, 3);
	gtk_widget_show(ret);
	return ret;
}

GtkWidget *create_pathtl(const char *wid) {
	GtkWidget *ret = gtk_pathtl_new();
	gtk_widget_show(ret);
	return ret;
}

gchar *zoom_format_value(GtkScale *scale, gdouble value) {
	return g_strdup_printf("%.2f", SLIDER_TO_ZOOM(value));
}
void zoom_value_changed(GtkRange *range) {
	gtk_dag_set_zoom(GTK_DAG(WID("dag")),
		SLIDER_TO_ZOOM(gtk_range_get_value(range)));
}

void plot_point_clicked(GtkPlot *plot, GtkPlotPoint *point) {
  if (point)
    printf("point clicked: %.3f %.3f\n", point->x, point->y);
  else
    printf("point cleared\n");
}

void dag_node_clicked(GtkDAG *dag, DAGNode *node) {
	int thread_id = GPOINTER_TO_INT(node->user_data);
	run_sqlf(&mysql, "SELECT * FROM %s_threads WHERE thread_id=%d", table_base, thread_id);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row = mysql_fetch_row(res);
	assert(row);
	assert(atoi(row[0]) == thread_id);
	PathThread thr(row);
	mysql_free_result(res);
	gtk_label_set_text(GTK_LABEL(WID("label_host")), thr.host.c_str());
	gtk_label_set_text(GTK_LABEL(WID("label_program")), thr.prog.c_str());
}

void tasks_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	printf("tasks_activate\n");
}

void hosts_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	printf("hosts_activate\n");
}

static Path *active_path = NULL;
void paths_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	GtkTreeIter iter;
	int pathid;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(paths), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(paths), &iter, 0, &pathid, -1);
	if (active_path) delete active_path;
	active_path = new Path(&mysql, table_base, pathid);
	gtk_pathtl_set(GTK_PATHTL(WID("pathtl")), active_path);
	fill_dag(GTK_DAG(WID("dag")), active_path);
}

void recognizers_activate_row(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *tvc) {
	printf("recognizers_activate\n");
}

void tasks_search(GtkEntry *entry) {
	fill_tasks(gtk_entry_get_text(entry));
}

void hosts_search(GtkEntry *entry) {
	fill_hosts(gtk_entry_get_text(entry));
}

void paths_search(GtkEntry *entry) {
	fill_paths(gtk_entry_get_text(entry));
}

void recognizers_search(GtkEntry *entry) {
	fill_recognizers(gtk_entry_get_text(entry));
}

void on_graph_quantity_changed(GtkComboBox *cb) {
	printf("graph quantity changed to %d/%s\n", gtk_combo_box_get_active(cb), gtk_combo_box_get_active_text(cb));
}

void on_graph_style_changed(GtkComboBox *cb) {
	printf("graph style changed to %d/%s\n", gtk_combo_box_get_active(cb), gtk_combo_box_get_active_text(cb));
}

void on_aggregation_changed(GtkComboBox *cb) {
	printf("aggregation changed to %d/%s\n", gtk_combo_box_get_active(cb), gtk_combo_box_get_active_text(cb));
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

static DAGNode *fill_dag_one_message(GtkDAG *dag, const PathMessageRecv *pmr) {
	if (dag_nodes[pmr]) return dag_nodes[pmr];
	DAGNode *parent = dag_nodes[pmr->send->pred];
	if (!parent) parent = fill_dag_one_message(dag, pmr->send->pred);
	assert(parent);
	dag_nodes[pmr] = gtk_dag_add_node(dag, itoa(pmr->thread_recv), parent, NULL, 0, GINT_TO_POINTER(pmr->thread_recv));
	return dag_nodes[pmr];
}

static void fill_dag_from_list(GtkDAG *dag, const PathEventList &list) {
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			case PEV_TASK:
				fill_dag_from_list(dag, ((PathTask*)list[i])->children);
				break;
			case PEV_MESSAGE_RECV:
				fill_dag_one_message(dag, ((PathMessageRecv*)list[i]));
				break;
			default: ;
		}
	}
}

static void fill_dag(GtkDAG *dag, const Path *path) {
	dag_nodes.clear();
	gtk_dag_freeze(dag);
	gtk_dag_clear(dag);
	dag_nodes[NULL] = gtk_dag_add_root(dag, itoa(path->root_thread), NULL, 0, GINT_TO_POINTER(path->root_thread));
	std::map<int, PathEventList>::const_iterator thread;
	for (thread=path->children.begin(); thread!=path->children.end(); thread++)
		fill_dag_from_list(dag, thread->second);
	gtk_dag_thaw(dag);
}
