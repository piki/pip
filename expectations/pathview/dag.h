#ifndef __GTK_DAG_H__
#define __GTK_DAG_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTK_DAG(obj)          GTK_CHECK_CAST(obj, gtk_dag_get_type(), GtkDAG)
#define GTK_DAG_CLASS(klass)  GTK_CHECK_CLASS_CAST(klass, gtk_dag_get_type(), GtkDAGClass)
#define GTK_IS_DAG(obj)       GTK_CHECK_TYPE(obj, gtk_dag_get_type())

typedef struct _GtkDAG       GtkDAG;
typedef struct _GtkDAGClass  GtkDAGClass;
typedef struct _DAGRoot      DAGRoot;
typedef struct _DAGNode      DAGNode;
typedef struct _DAGEdge      DAGEdge;

struct _GtkDAG {
	GtkWidget widget;
	GPtrArray *trees;
	double zoom;

	/* private */
	gboolean frozen;
	DAGNode *button_down;
	PangoLayout *pango;
	PangoAttrList *pango_attributes;
};

struct _GtkDAGClass {
	GtkWidgetClass parent_class;

	void (* node_clicked) (GtkDAG *dag, DAGNode *node);
};

struct _DAGNode {
	char *label;
	GArray *edges;
	gpointer user_data;
	int brightness;

	/* private */
	int seen, rank, xpos, ypos;
};

struct _DAGRoot {
	char *caption;
	DAGNode *node;
};
#define DAG_ROOT(root) ((DAGRoot*)(root))
#define DR_NODE(root) DAG_ROOT(root)->node

struct _DAGEdge {
	char *labels[4];
	DAGNode *dest;
};

GType      gtk_dag_get_type();
GtkWidget* gtk_dag_new(void);
void       gtk_dag_clear(GtkDAG *dag);
/* label = contents of the bubble
 * caption = title displayed above the DAG
 * brightness = how highlighted (red) the node is, 0..255 */
DAGNode*   gtk_dag_add_root(GtkDAG *dag, const char *label,
		const char *caption, int brightness, gpointer user_data);
/* label = contents of the bubble
 * parent = DAGNode created by previous add_root or add_node call
 * edge_labels = top, bottom, left, right of the line
 * brightness = how highlighted (red) the node is, 0..255 */
DAGNode*   gtk_dag_add_node(GtkDAG *dag, const char *label, DAGNode *parent,
		const char **edge_labels, int brightness, gpointer user_data);
void       gtk_dag_add_edge(GtkDAG *dag, DAGNode *from, DAGNode *to,
		const char **labels);
void       gtk_dag_freeze(GtkDAG *dag);
void       gtk_dag_thaw(GtkDAG *dag);
void       gtk_dag_set_zoom(GtkDAG *dag, double zoom);

#ifdef __cplusplus
}
#endif

#endif
