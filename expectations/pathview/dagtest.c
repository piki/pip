#include <math.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include "dag.h"

static void clicked(GtkDAG *dag, DAGNode *node);
static gchar *log_format_value(GtkScale *scale, gdouble value);
static void new_zoom(GtkAdjustment *adj, GtkDAG *dag);

int main(int argc, char **argv) {
	GtkWidget *w, *dag, *scroller, *vbox;

	gtk_init(&argc, &argv);

	w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(w), 640, 480);
	vbox = gtk_vbox_new(FALSE, 0);
	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	dag = gtk_dag_new();
	gtk_container_add(GTK_CONTAINER(w), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), scroller, TRUE, TRUE, 0);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroller), dag);
	g_signal_connect(GTK_OBJECT(w), "delete_event", gtk_main_quit, NULL);
	g_signal_connect(GTK_OBJECT(dag), "node_clicked",
		GTK_SIGNAL_FUNC(clicked), "whee");

	GtkObject *adj = gtk_adjustment_new(3, 0, 5, 0.02, 0.1, 0);
	GtkWidget *slider = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_range_set_update_policy(GTK_RANGE(slider), GTK_UPDATE_CONTINUOUS);
	gtk_scale_set_digits(GTK_SCALE(slider), 2);
	gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_RIGHT);
	gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
	g_signal_connect(G_OBJECT(slider), "format_value",
		G_CALLBACK(log_format_value), NULL);
	g_signal_connect(G_OBJECT(adj), "value_changed",
		G_CALLBACK(new_zoom), dag);
	gtk_box_pack_start(GTK_BOX(vbox), slider, FALSE, FALSE, 0);

	gtk_dag_freeze(GTK_DAG(dag));
	const char *edge[4] = { "top", "bottom", "left", "right" };
	DAGNode *node = gtk_dag_add_root(GTK_DAG(dag), "root", "caption", 0, "r");
	gtk_dag_add_node(GTK_DAG(dag), "child", node, edge, 255, "c1");
	node = gtk_dag_add_node(GTK_DAG(dag), "child 2", node, edge, 64, "c2");
	gtk_dag_add_node(GTK_DAG(dag), "grandchild", node, edge, 128, "gc1");
	gtk_dag_add_node(GTK_DAG(dag), "grandchild 2", node, edge, 192, "gc2");
	gtk_dag_thaw(GTK_DAG(dag));

	gtk_widget_show_all(w);
	gtk_main();

	return 0;
}

static void clicked(GtkDAG *dag, DAGNode *node) {
	const char *s = node->user_data;
	printf("clicked(\"%s\"): \"%s\"\n", node->label, s);
}

#define SLIDER_TO_ZOOM(z) pow(2, (z)-3)

static gchar *log_format_value(GtkScale *scale, gdouble value) {
	return g_strdup_printf("%.2f", SLIDER_TO_ZOOM(value));
}

static void new_zoom(GtkAdjustment *adj, GtkDAG *dag) {
	gtk_dag_set_zoom(dag, SLIDER_TO_ZOOM(adj->value));
}
