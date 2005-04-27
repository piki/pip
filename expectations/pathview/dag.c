#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtksignal.h>

#include "dag.h"

#define MARGIN 10
#define CAPTION 0
#define NODE_WIDTH 40
#define NODE_HEIGHT 22
#define NODE_XGAP 10
#define NODE_YGAP 15
#define TREE_XGAP 100
#define MIN_ZOOM_TEXT_VISIBLE 0.6
#define LABEL_XSCROLL_SLOP 300 /* upper bound on label width */

#define MAX_WIDTH 30000

enum {
	NODE_CLICKED,
	LAST_SIGNAL
};

static guint dag_signals[LAST_SIGNAL] = { 0 };

static void gtk_dag_class_init     (GtkDAGClass       *klass);
static void gtk_dag_init           (GtkDAG            *dag);
static void gtk_dag_realize        (GtkWidget         *widget);
static void gtk_dag_size_allocate  (GtkWidget         *widget,
                                    GtkAllocation     *allocation);
static gint gtk_dag_expose         (GtkWidget         *widget,
                                    GdkEventExpose    *ev);
static gint gtk_dag_button_press   (GtkWidget         *widget,
                                    GdkEventButton    *ev);
static gint gtk_dag_button_release (GtkWidget         *widget,
                                    GdkEventButton    *ev);

static void gtk_dag_update(GtkDAG *dag);
static void gtk_dag_layout(GtkDAG *dag);

typedef void DAGFunc(DAGNode *node);
static void dag_node_unset_seen(DAGNode *node);
static void dag_node_inc_seen(DAGNode *node);
static void dag_node_free(DAGNode *node);
static void dag_node_for_each(DAGNode *node, DAGFunc func);
static DAGNode *dag_node_new(const char *label, int brightness,
		gpointer user_data);
static void dag_node_unset_seen(DAGNode *node);

GType gtk_dag_get_type() {
  static GType dag_type = 0;

  if (!dag_type) {
    GTypeInfo dag_info = {
      sizeof(GtkDAGClass),
			NULL,
			NULL,
      (GClassInitFunc)gtk_dag_class_init,
			NULL,
			NULL,
      sizeof(GtkDAG),
			0,
      (GtkObjectInitFunc)gtk_dag_init,
    };
    
    dag_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkDAG", &dag_info, 0);
  }
  
  return dag_type;
}

void gtk_dag_clear(GtkDAG *dag) {
	int i;

	g_return_if_fail(dag);
	g_return_if_fail(GTK_IS_DAG(dag));

	/* how to free the DAG: set node->seen to a reference count for each
	 * node, then call dag_node_free, which unrefs each node and only frees
	 * it when refcount reaches zero */
	for (i=0; i<dag->trees->len; i++) {
		dag_node_for_each(DR_NODE(g_ptr_array_index(dag->trees, i)),
			dag_node_unset_seen);
		dag_node_for_each(DR_NODE(g_ptr_array_index(dag->trees, i)),
			dag_node_inc_seen);
	}
	for (i=0; i<dag->trees->len; i++) {
		DAGRoot *root = g_ptr_array_index(dag->trees, i);
		dag_node_free(root->node);
		if (root->caption) g_free(root->caption);
		g_free(root);
	}
	g_ptr_array_free(dag->trees, TRUE);
	dag->trees = g_ptr_array_new();
	dag->button_down = NULL;

	if (!dag->frozen) gtk_dag_update(dag);
}

static void gtk_dag_class_init(GtkDAGClass *class) {
  GtkWidgetClass *widget_class = (GtkWidgetClass*)class;

  dag_signals[NODE_CLICKED] =
    g_signal_new("node_clicked",
      G_TYPE_FROM_CLASS(class),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET(GtkDAGClass, node_clicked),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
      GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

  widget_class->realize = gtk_dag_realize;
  widget_class->size_allocate = gtk_dag_size_allocate;
  widget_class->expose_event = gtk_dag_expose;
	widget_class->button_press_event = gtk_dag_button_press;
	widget_class->button_release_event = gtk_dag_button_release;
  
  /*class->value_changed = NULL;*/
}

static void gtk_dag_init(GtkDAG *dag) {
	PangoAttribute *pattr;

	dag->trees = g_ptr_array_new();
	dag->frozen = FALSE;
	dag->button_down = NULL;
	dag->zoom = 1.0;
	dag->pango = gtk_widget_create_pango_layout(GTK_WIDGET(dag), "");
	dag->pango_attributes = pango_attr_list_new();
	pattr = pango_attr_size_new(10*1000);
	pattr->start_index = 0;
	pattr->end_index = 200;
	pango_attr_list_insert(dag->pango_attributes, pattr);
	pango_layout_set_attributes(dag->pango, dag->pango_attributes);

	gtk_widget_set_events(GTK_WIDGET(dag),
		GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
}

GtkWidget *gtk_dag_new(void) {
  GtkWidget *dag;

  dag = gtk_type_new(gtk_dag_get_type());
  dag->requisition.width = 4000;
  dag->requisition.height = 3000;

  return dag;
}

void gtk_dag_freeze(GtkDAG *dag) {
	g_return_if_fail(dag);
	g_return_if_fail(GTK_IS_DAG(dag));

	dag->frozen = TRUE;
}

void gtk_dag_thaw(GtkDAG *dag) {
	g_return_if_fail(dag);
	g_return_if_fail(GTK_IS_DAG(dag));

	dag->frozen = FALSE;
	gtk_dag_update(dag);
	gtk_widget_queue_draw(GTK_WIDGET(dag));
}

DAGNode* gtk_dag_add_node(GtkDAG *dag, const char *label, DAGNode *parent,
		const char **edge_labels, int brightness, gpointer user_data) {
	DAGNode *node;
	int i;
	DAGEdge e;

	g_return_val_if_fail(dag, NULL);
	g_return_val_if_fail(GTK_IS_DAG(dag), NULL);
	g_return_val_if_fail(brightness >= 0 && brightness <= 255, NULL);
	g_return_val_if_fail(parent, NULL);

	node = dag_node_new(label, brightness, user_data);
	for (i=0; i<4; i++)
		e.labels[i] = edge_labels && edge_labels[i]
			? g_strdup(edge_labels[i])
			: NULL;
	e.dest = node;
	g_array_append_val(parent->edges, e);

	if (!dag->frozen) gtk_dag_update(dag);

	return node;
}

DAGNode* gtk_dag_add_root(GtkDAG *dag, const char *label, const char *caption,
		int brightness, gpointer user_data) {
	DAGNode *node;
	DAGRoot *root;

	g_return_val_if_fail(dag, NULL);
	g_return_val_if_fail(GTK_IS_DAG(dag), NULL);
	g_return_val_if_fail(brightness >= 0 && brightness <= 255, NULL);

	node = dag_node_new(label, brightness, user_data);
	root = g_new(DAGRoot, 1);
	root->node = node;
	root->caption = g_strdup(caption);
	g_ptr_array_add(dag->trees, root);

	if (!dag->frozen) gtk_dag_update(dag);

	return node;
}

void gtk_dag_add_edge(GtkDAG *dag, DAGNode *from, DAGNode *to,
		const char **edge_labels) {
	DAGEdge e;
	int i;

	g_return_if_fail(dag);
	g_return_if_fail(GTK_IS_DAG(dag));

	for (i=0; i<4; i++)
		e.labels[i] = edge_labels[i] ? g_strdup(edge_labels[i]) : NULL;
	e.dest = to;

	g_array_append_val(from->edges, e);

	if (!dag->frozen) gtk_dag_update(dag);
}

void gtk_dag_set_zoom(GtkDAG *dag, double zoom) {
	g_return_if_fail(dag);
	g_return_if_fail(GTK_IS_DAG(dag));

	dag->zoom = zoom;
	if (!dag->frozen) gtk_dag_update(dag);
}

static void gtk_dag_update(GtkDAG *dag) {
	g_return_if_fail(dag);
	g_return_if_fail(GTK_IS_DAG(dag));
	g_return_if_fail(!dag->frozen);

	if (dag->trees->len > 0) {
		gtk_dag_layout(dag);
		gtk_widget_queue_draw(GTK_WIDGET(dag));
	}
}

static void dag_node_unrank(DAGNode *node) {
	node->seen = 0;
	node->rank = node->xpos = node->ypos = -1;
}
static void dag_node_rank(DAGNode *node, int rank) {
	int i;
	if (node->rank != -1) return;

	node->rank = rank;
	node->ypos = CAPTION + MARGIN + rank * (NODE_HEIGHT+NODE_YGAP);
	for (i=0; i<node->edges->len; i++)
		dag_node_rank(g_array_index(node->edges, DAGEdge, i).dest, rank+1);
}
static int dag_node_assign_x(DAGNode *node, int leftx) {
	int i, curx;
	int new_children = 0;
	if (node->xpos != -1) return -1;
	if (node->seen++) {
		fprintf(stderr, "ERROR: graph is not acyclic!\n");
		exit(1);
	}

	curx = leftx;
	for (i=0; i<node->edges->len; i++) {
		int newx =
			dag_node_assign_x(g_array_index(node->edges, DAGEdge, i).dest, curx);
		if (newx != -1) {
			new_children = 1;
			curx = newx + NODE_XGAP;
		}
	}
	if (new_children) {
		curx -= NODE_XGAP;
		node->xpos = (leftx + curx - NODE_WIDTH)/2;
		return curx;
	}
	else {
		node->xpos = leftx;
		return leftx + NODE_WIDTH;
	}
}
static void dag_node_get_max_xy(DAGNode *node, int *maxx, int *maxy) {
	int i;
	if (node->xpos > *maxx) *maxx = node->xpos;
	if (node->ypos > *maxy) *maxy = node->ypos;
	for (i=0; i<node->edges->len; i++)
		dag_node_get_max_xy(g_array_index(node->edges, DAGEdge, i).dest, maxx, maxy);
}

static void gtk_dag_layout(GtkDAG *dag) {
	int i, x=MARGIN, width=0, height=0;
	PangoAttribute *pattr;

	g_return_if_fail(dag->trees->len > 0);

	pattr = pango_attr_size_new(dag->zoom * 10*1000);
	pattr->start_index = 0;
	pattr->end_index = 200;
	pango_attr_list_change(dag->pango_attributes, pattr);

	/* assign ranks */
	for (i=0; i<dag->trees->len; i++) {
		dag_node_for_each(DR_NODE(g_ptr_array_index(dag->trees, i)),
			dag_node_unrank);
		dag_node_rank(DR_NODE(g_ptr_array_index(dag->trees, i)), 0);
	}

	/* assign X values */
	for (i=0; i<dag->trees->len; i++) {
		x = dag_node_assign_x(DR_NODE(g_ptr_array_index(dag->trees, i)), x)
			+ TREE_XGAP;
		if (x >= MAX_WIDTH/dag->zoom) break;
	}

	/* get extents */
	for (i=0; i<dag->trees->len; i++) {
		DAGNode *root = DR_NODE(g_ptr_array_index(dag->trees, i));
		if (root->xpos == -1) break;
		dag_node_get_max_xy(root, &width, &height);
	}
	width += NODE_WIDTH + 1 + MARGIN;
	height += NODE_HEIGHT + 1 + MARGIN;

	gtk_widget_set_usize(GTK_WIDGET(dag), dag->zoom*width, dag->zoom*height);
}

static void gtk_dag_realize(GtkWidget *widget) {
  GtkDAG *dag;
  GdkWindowAttr attributes;
  gint attributes_mask;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(GTK_IS_DAG(widget));
  
  dag = GTK_DAG(widget);
  GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
  
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual(widget);
  attributes.colormap = gtk_widget_get_colormap(widget);
  attributes.event_mask = gtk_widget_get_events(widget);
  attributes.event_mask |= GDK_EXPOSURE_MASK;
  
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  
  widget->window = gdk_window_new(gtk_widget_get_parent_window(widget), &attributes, attributes_mask);
  gdk_window_set_user_data(widget->window, dag);
  
  widget->style = gtk_style_attach(widget->style, widget->window);
  gtk_style_set_background(widget->style, widget->window, GTK_STATE_ACTIVE);
}

static void gtk_dag_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
  g_return_if_fail(widget != NULL);
  g_return_if_fail(GTK_IS_DAG(widget));
  g_return_if_fail(allocation != NULL);

  widget->allocation = *allocation;
  
  if (GTK_WIDGET_REALIZED(widget))
    gdk_window_move_resize(widget->window,
      allocation->x, allocation->y, allocation->width, allocation->height);
}

static GdkColor black = {0};
static GdkColor white = {0};
static GdkColor yellow = {0,0xff00,0xff00,0x6600};
static void dag_node_draw_nodes(GtkDAG *dag, GdkDrawable *draw, GdkGC *gc,
		DAGNode *node, GdkRectangle *area) {
	int i, width, height, x, y;

	if (node->seen++) return;

	if (dag->button_down == node)
		gdk_gc_set_foreground(gc, &yellow);
	else {
		int b = node->brightness;
		GdkColor c = {0};
		c.red =   (b * 0xff00 + (255-b) * 0xcc00) / 255;
		c.green = (b * 0x6600 + (255-b) * 0xcc00) / 255;
		c.blue =  (b * 0x7700 + (255-b) * 0xcc00) / 255;
		gdk_color_alloc(gtk_widget_get_colormap(GTK_WIDGET(dag)), &c);
		gdk_gc_set_foreground(gc, &c);
	}

	x = dag->zoom * node->xpos - area->x;
	if (x + dag->zoom*NODE_WIDTH < 0) goto children;
	if (x > area->width) goto children;
	y = dag->zoom * node->ypos - area->y;
	if (y + dag->zoom*NODE_HEIGHT < 0) goto children;
	if (y > area->height) return;
	gdk_draw_arc(draw, gc, TRUE, x, y, dag->zoom*NODE_WIDTH, dag->zoom*NODE_HEIGHT, 0, 64*360);
	gdk_gc_set_foreground(gc, &black);
	gdk_draw_arc(draw, gc, FALSE, x, y, dag->zoom*NODE_WIDTH, dag->zoom*NODE_HEIGHT, 0, 64*360);
	if (dag->zoom >= MIN_ZOOM_TEXT_VISIBLE) {
		pango_layout_set_text(dag->pango, node->label, -1);
		pango_layout_get_size(dag->pango, &width, &height);
		width /= PANGO_SCALE;
		height /= PANGO_SCALE;
		gdk_draw_layout(draw, gc, x+dag->zoom*NODE_WIDTH/2-width/2,
				y+dag->zoom*NODE_HEIGHT/2-height/2, dag->pango);
	}

children:
	for (i=0; i<node->edges->len; i++) {
		DAGNode *child = g_array_index(node->edges, DAGEdge, i).dest;
		dag_node_draw_nodes(dag, draw, gc, child, area);
	}
}

static void dag_node_draw_edges(GtkDAG *dag, GdkDrawable *draw, GdkGC *gc,
		DAGNode *node, GdkRectangle *area) {
	int i;

	if (node->seen++) return;

	for (i=0; i<node->edges->len; i++) {
		DAGEdge *edge = &g_array_index(node->edges, DAGEdge, i);
		DAGNode *child = edge->dest;
		int x1 = dag->zoom*(node->xpos+NODE_WIDTH/2) - area->x;
		int y1 = dag->zoom*(node->ypos+NODE_HEIGHT/2) - area->y;
		int x2 = dag->zoom*(child->xpos+NODE_WIDTH/2) - area->x;
		int y2 = dag->zoom*child->ypos - area->y;
		int minx = MIN(x1, x2);
		int maxx = MAX(x1, x2);
		g_assert(y2 > y1);

		dag_node_draw_edges(dag, draw, gc, child, area);
		if (minx - LABEL_XSCROLL_SLOP*dag->zoom > area->width) continue;
		if (maxx + LABEL_XSCROLL_SLOP*dag->zoom < 0) continue;
		if (y1 > area->height) continue;
		if (y2 < 0) continue;
		if (dag->zoom >= MIN_ZOOM_TEXT_VISIBLE) {
			int width, height, x, y;
			if (edge->labels[0]) {  /* top */
				pango_layout_set_text(dag->pango, edge->labels[0], -1);
				pango_layout_get_size(dag->pango, &width, &height);
				width /= PANGO_SCALE;  height /= PANGO_SCALE;
				y = y1+dag->zoom*NODE_HEIGHT/2;
				if (x1 == x2) x = x1 + 2;
				else if (x1 < x2) x = x1 + (x2-x1)*(y-y1)/(y2-y1) + height*(x2-x1)/(y2-y1);
				else /* x1 > x2 */ x = x1 + (x2-x1)*(y-y1)/(y2-y1) - width + height*(x2-x1)/(y2-y1);
				gdk_draw_layout(draw, gc, x, y, dag->pango);
			}
			if (edge->labels[1]) {  /* bottom */
				pango_layout_set_text(dag->pango, edge->labels[1], -1);
				pango_layout_get_size(dag->pango, &width, &height);
				width /= PANGO_SCALE;  height /= PANGO_SCALE;
				if (x1 == x2) x = x2 + 2;
				else if (x1 < x2) x = x2;
				else /* x1 > x2 */ x = x2 - width;
				y = y2-height;
				gdk_draw_layout(draw, gc, x, y, dag->pango);
			}
			if (edge->labels[2]) {  /* left */
				pango_layout_set_text(dag->pango, edge->labels[2], -1);
				pango_layout_get_size(dag->pango, &width, &height);
				width /= PANGO_SCALE;  height /= PANGO_SCALE;
				if (x1 == x2) { x = x1-width-2;  y = (y1+2*y2)/3-height/2; }
				else if (x1 < x2) { x = (x1+2*x2)/3-width-2; y = (y1+2*y2)/3; }
				else /* x1 > x2 */ { x = (x1+2*x2)/3-width-2; y = (y1+2*y2)/3-height+4; }
				gdk_draw_layout(draw, gc, x, y, dag->pango);
			}
			if (edge->labels[3]) {  /* right */
				pango_layout_set_text(dag->pango, edge->labels[3], -1);
				pango_layout_get_size(dag->pango, &width, &height);
				width /= PANGO_SCALE;  height /= PANGO_SCALE;
				if (x1 == x2) { x = x1+2;  y = (y1+2*y2)/3-height/2; }
				else if (x1 < x2) { x = (x1+2*x2)/3+2; y = (y1+2*y2)/3-height+4; }
				else /* x1 > x2 */ { x = (x1+2*x2)/3+2; y = (y1+2*y2)/3; }
				gdk_draw_layout(draw, gc, x, y, dag->pango);
			}
		}
		gdk_draw_line(draw, gc, x1, y1, x2, y2);
	}
}

static gint gtk_dag_expose(GtkWidget *widget, GdkEventExpose *ev) {
	GtkDAG *dag;
	GdkGC *gc;
	GdkPixmap *drawbuffer;
	int i;

  g_return_val_if_fail(widget != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_DAG(widget), FALSE);
  g_return_val_if_fail(ev != NULL, FALSE);
	g_return_val_if_fail(GTK_WIDGET_DRAWABLE(widget), FALSE);

	dag = GTK_DAG(widget);
	g_return_val_if_fail(!dag->frozen, FALSE);

	if (white.pixel == 0) {
		GdkColormap *cmap = gdk_window_get_colormap(widget->window);
		gdk_color_black(cmap, &black);
		gdk_color_white(cmap, &white);
		gdk_color_alloc(cmap, &yellow);
	}

#if 0
	printf("expose %d,%d  %dx%d  %d roots\n",
		ev->area.x, ev->area.y, ev->area.width, ev->area.height, dag->trees->len);
#endif

	drawbuffer = gdk_pixmap_new(widget->window,
		ev->area.width, ev->area.height, -1);
	gc = gdk_gc_new(drawbuffer);
	gdk_gc_set_foreground(gc, &white);
	gdk_draw_rectangle(drawbuffer, gc, TRUE, 0, 0,
		ev->area.width, ev->area.height);
	gdk_gc_set_foreground(gc, &black);

	/* draw captions */
	if (dag->zoom >= MIN_ZOOM_TEXT_VISIBLE) {
		pango_layout_set_alignment(dag->pango, PANGO_ALIGN_CENTER);
		for (i=0; i<dag->trees->len; i++) {
			DAGRoot *root = g_ptr_array_index(dag->trees, i);
			if (root->caption) {
				GtkDAG *dag = GTK_DAG(widget);
				int x = dag->zoom * root->node->xpos - ev->area.x;
				int width, height;
				if (x + LABEL_XSCROLL_SLOP < 0) continue;
				if (x - LABEL_XSCROLL_SLOP > ev->area.width) break;
				pango_layout_set_text(dag->pango, root->caption, -1);
				pango_layout_get_size(dag->pango, &width, &height);
				width /= PANGO_SCALE;
				height /= PANGO_SCALE;
				gdk_draw_layout(drawbuffer, gc, x+dag->zoom*NODE_WIDTH/2-width/2,
						MARGIN*dag->zoom - ev->area.y, dag->pango);
			}
		}
	}

	/* draw edges */
	pango_layout_set_alignment(dag->pango, PANGO_ALIGN_LEFT);
	for (i=0; i<dag->trees->len; i++)
		dag_node_for_each(DR_NODE(g_ptr_array_index(dag->trees, i)),
			dag_node_unset_seen);
	for (i=0; i<dag->trees->len; i++) {
		DAGNode *root = DR_NODE(g_ptr_array_index(dag->trees, i));
		if (root->xpos == -1) break;
		dag_node_draw_edges(dag, drawbuffer, gc, root, &ev->area);
	}

	/* draw nodes */
	pango_layout_set_alignment(dag->pango, PANGO_ALIGN_CENTER);
	for (i=0; i<dag->trees->len; i++)
		dag_node_for_each(DR_NODE(g_ptr_array_index(dag->trees, i)),
			dag_node_unset_seen);
	for (i=0; i<dag->trees->len; i++) {
		DAGNode *root = DR_NODE(g_ptr_array_index(dag->trees, i));
		if (root->xpos == -1) break;
		dag_node_draw_nodes(dag, drawbuffer, gc, root, &ev->area);
	}
	gdk_gc_destroy(gc);

	gc = gdk_gc_new(widget->window);
	gdk_draw_pixmap(widget->window, gc, drawbuffer, 0, 0,
		ev->area.x, ev->area.y, ev->area.width, ev->area.height);
	gdk_pixmap_unref(drawbuffer);
	gdk_gc_destroy(gc);

  return TRUE;
}

static DAGNode *dag_node_find_click(DAGNode *node, int x, int y) {
	int i;

	if (x >= node->xpos && y >= node->ypos
			&& x < node->xpos+NODE_WIDTH && y < node->ypos+NODE_HEIGHT)
		return node;

	for (i=0; i<node->edges->len; i++) {
		DAGNode *temp =
			dag_node_find_click(g_array_index(node->edges, DAGEdge, i).dest, x, y);
		if (temp) return temp;
	}

	return NULL;
}

static DAGNode *gtk_dag_find_click(GtkDAG *dag, int x, int y) {
	int i;
	DAGNode *n;

	for (i=0; i<dag->trees->len; i++)
		if ((n = dag_node_find_click(DR_NODE(g_ptr_array_index(dag->trees, i)),
				x/dag->zoom, y/dag->zoom)) != NULL)
			return n;

	return NULL;
}

static gint gtk_dag_button_press(GtkWidget *widget, GdkEventButton *ev) {
	GtkDAG *dag;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_DAG(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	dag = GTK_DAG(widget);
	dag->button_down = gtk_dag_find_click(dag, (int)ev->x, (int)ev->y);

	gtk_widget_queue_draw(widget);

	return dag->button_down != NULL;
}

static gint gtk_dag_button_release(GtkWidget *widget, GdkEventButton *ev) {
	DAGNode *where;
	GtkDAG *dag;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_DAG(widget), FALSE);
	dag = GTK_DAG(widget);
	where = gtk_dag_find_click(dag, (int)ev->x, (int)ev->y);
	if (where != NULL && where == dag->button_down)
		g_signal_emit(G_OBJECT(dag), dag_signals[NODE_CLICKED], 0, where);

	gtk_widget_queue_draw(widget);

	return TRUE;
}

static void dag_node_unset_seen(DAGNode *node) { node->seen = 0; }
static void dag_node_inc_seen(DAGNode *node) { node->seen++; }
static void dag_node_free(DAGNode *node) {
	int i;
	for (i=0; i<node->edges->len; i++)
		dag_node_free(g_array_index(node->edges, DAGEdge, i).dest);
	if (--node->seen == 0) {
		g_array_free(node->edges, TRUE);
		g_free(node);
	}
}

static void dag_node_for_each(DAGNode *node, DAGFunc func) {
	int i;
	func(node);
	for (i=0; i<node->edges->len; i++)
		dag_node_for_each(g_array_index(node->edges, DAGEdge, i).dest, func);
}

static DAGNode *dag_node_new(const char *label, int brightness,
		gpointer user_data) {
	DAGNode *node;

	node = g_new(DAGNode, 1);
	node->label = (label && label[0]) ? g_strdup(label) : NULL;
	node->edges = g_array_new(FALSE, FALSE, sizeof(DAGEdge));
	node->seen = 0;
	node->rank = node->xpos = node->ypos = -1;
	node->brightness = brightness;
	node->user_data = user_data;

	return node;
}
