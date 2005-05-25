#ifndef __GTK_GRAPH_H__
#define __GTK_GRAPH_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTK_GRAPH(obj)          GTK_CHECK_CAST(obj, gtk_graph_get_type(), GtkGraph)
#define GTK_GRAPH_CLASS(klass)  GTK_CHECK_CLASS_CAST(klass, gtk_graph_get_type(), GtkGraphClass)
#define GTK_IS_GRAPH(obj)       GTK_CHECK_TYPE(obj, gtk_graph_get_type())

typedef struct _GtkGraph       GtkGraph;
typedef struct _GtkGraphClass  GtkGraphClass;
typedef struct _GtkGraphNode   GtkGraphNode;
typedef struct _GtkGraphEdge   GtkGraphEdge;

struct _GtkGraph {
	GtkWidget widget;
	GPtrArray *nodes, *edges;
	gboolean frozen, needs_layout;
};

struct _GtkGraphClass {
	GtkWidgetClass parent_class;

	void (* node_clicked) (GtkGraph *graph, GtkGraphNode *node);
};

struct _GtkGraphNode {
	char *label;
	double x, y;
};

struct _GtkGraphEdge {
	GtkGraphNode *a, *b;
	GArray *points;
	gboolean directed;
};

GType         gtk_graph_get_type();
GtkWidget*    gtk_graph_new(void);
void          gtk_graph_free(GtkGraph *graph);
void          gtk_graph_clear(GtkGraph *graph);
GtkGraphNode* gtk_graph_add_node(GtkGraph *graph, const char *label);
GtkGraphEdge* gtk_graph_add_edge(GtkGraph *graph, GtkGraphNode *a, GtkGraphNode *b, gboolean directed);
void          gtk_graph_freeze(GtkGraph *graph);
void          gtk_graph_thaw(GtkGraph *graph);
/* remove all duplicate edges, and replace all to+from directed edges with
 * a single, undirected edge */
void          gtk_graph_simplify(GtkGraph *graph);

#ifdef __cplusplus
}
#endif

#endif
