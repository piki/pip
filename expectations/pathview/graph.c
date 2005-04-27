#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtksignal.h>

#include "graph.h"

enum {
  NODE_CLICKED,
  LAST_SIGNAL
};

static guint graph_signals[LAST_SIGNAL] = { 0 };

static void gtk_graph_class_init     (GtkGraphClass      *klass);
static void gtk_graph_init           (GtkGraph           *graph);
static void gtk_graph_realize        (GtkWidget         *widget);
static void gtk_graph_size_allocate  (GtkWidget         *widget,
                                      GtkAllocation      *allocation);
static gint gtk_graph_expose         (GtkWidget         *widget,
                                      GdkEventExpose     *ev);
static gint gtk_graph_button_press   (GtkWidget         *widget,
                                      GdkEventButton     *ev);
static gint gtk_graph_button_release (GtkWidget         *widget,
                                      GdkEventButton     *ev);
static gint gtk_graph_motion         (GtkWidget         *widget,
                                      GdkEventMotion     *ev);

static void gtk_graph_update(GtkGraph *graph);
static void gtk_graph_layout(GtkGraph *graph);

#define X_MARGIN 2
#define Y_MARGIN 2
#define ARROW_SIZE 20
#define ARROW_WIDTH 12

#define NODE(graph, i) ((GtkGraphNode*)g_ptr_array_index((graph)->nodes, (i)))
#define EDGE(graph, i) ((GtkGraphEdge*)g_ptr_array_index((graph)->edges, (i)))

GType gtk_graph_get_type() {
  static GType graph_type = 0;

  if (!graph_type) {
    static const GTypeInfo graph_info = {
      sizeof(GtkGraphClass),
			NULL,
			NULL,
      (GClassInitFunc)gtk_graph_class_init,
			NULL,
			NULL,
      sizeof(GtkGraph),
			0,
      (GInstanceInitFunc)gtk_graph_init,
    };
    
    graph_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkGraph", &graph_info, 0);
  }
  
  return graph_type;
}

void gtk_graph_clear(GtkGraph *graph) {
	int i;

	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));

	for (i=0; i<graph->nodes->len; i++) {
		if (NODE(graph, i)->label) g_free(NODE(graph, i)->label);
		g_free(NODE(graph, i));
	}
	for (i=0; i<graph->edges->len; i++)
		g_free(EDGE(graph, i));

	if (!graph->frozen) gtk_graph_update(graph);
}

static void gtk_graph_class_init(GtkGraphClass *class) {
  GtkWidgetClass *widget_class = (GtkWidgetClass*)class;

  graph_signals[NODE_CLICKED] =
    g_signal_new("node_clicked",
      G_TYPE_FROM_CLASS(class),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET(GtkGraphClass, node_clicked),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

  widget_class->realize = gtk_graph_realize;
  widget_class->size_allocate = gtk_graph_size_allocate;
  widget_class->expose_event = gtk_graph_expose;
	widget_class->button_press_event = gtk_graph_button_press;
	widget_class->button_release_event = gtk_graph_button_release;
	widget_class->motion_notify_event = gtk_graph_motion;
  
  /*class->value_changed = NULL;*/
}

static void gtk_graph_init(GtkGraph *graph) {
	graph->nodes = g_ptr_array_new();
	graph->edges = g_ptr_array_new();
	graph->frozen = FALSE;

	gtk_widget_set_events(GTK_WIDGET(graph), GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_BUTTON1_MOTION_MASK);
}

GtkWidget *gtk_graph_new(void) {
  GtkWidget *graph;

  graph = gtk_type_new(gtk_graph_get_type());
  graph->requisition.width = 160;
  graph->requisition.height = 120;

  return graph;
}

void gtk_graph_free(GtkGraph *graph) {
	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));
	graph->frozen = TRUE;
	gtk_graph_clear(graph);
	g_ptr_array_free(graph->nodes, TRUE);
	g_ptr_array_free(graph->edges, TRUE);
	g_free(graph);
}

void gtk_graph_freeze(GtkGraph *graph) {
	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));

	graph->frozen = TRUE;
}

void gtk_graph_thaw(GtkGraph *graph) {
	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));

	graph->frozen = FALSE;
	gtk_graph_update(graph);
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

GtkGraphNode *gtk_graph_add_node(GtkGraph *graph, const char *label) {
	GtkGraphNode *ret;

	g_return_val_if_fail(graph, NULL);
	g_return_val_if_fail(GTK_IS_GRAPH(graph), NULL);

	ret = g_new(GtkGraphNode, 1);
	ret->x = rand() % 320;
	ret->y = rand() % 240;
	ret->label = label ? g_strdup(label) : NULL;

	g_ptr_array_add(graph->nodes, ret);

	if (!graph->frozen) gtk_graph_update(graph);

	return ret;
}

void gtk_graph_add_edge(GtkGraph *graph, GtkGraphNode *a, GtkGraphNode *b, gboolean directed) {
	GtkGraphEdge *e;

	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));
	g_return_if_fail(a);
	g_return_if_fail(b);

	e = g_new(GtkGraphEdge, 1);
	e->a = a;
	e->b = b;
	e->directed = directed;

	g_ptr_array_add(graph->edges, e);

	if (!graph->frozen) gtk_graph_update(graph);
}

static void gtk_graph_update(GtkGraph *graph) {
	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));
	g_return_if_fail(!graph->frozen);

	gtk_graph_layout(graph);
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

#define GRAVITY 50.0
#define SPRING 0.02
#define SPRINGLEN 110
#define DAMP 1.0
#define ROUNDS 50000
#define MOVE_MIN 0.00
#define MAX_GRAV_DIST 200
//#define DEBUG

static void gtk_graph_layout(GtkGraph *graph) {
	if (graph->nodes->len == 0) return;

	gboolean was_frozen = graph->frozen;
	graph->frozen = TRUE;
	int i, j, k;
	for (k=0; k<ROUNDS; k++) {
		/* apply anti-gravity */
		for (i=0; i<graph->nodes->len; i++) {
			GtkGraphNode *n = NODE(graph, i);
			n->nx = n->x;
			n->ny = n->y;
#ifdef DEBUG
			printf("Node %d (%s) starts at %.2f,%.2f\n", i, n->label, n->x, n->y);
#endif
			for (j=0; j<graph->nodes->len; j++) {
				if (i == j) continue;
				GtkGraphNode *n2 = NODE(graph, j);
				double dx = n->x - n2->x;
				double dy = n->y - n2->y;
				double dist = hypot(dx, dy);
				if (dist > MAX_GRAV_DIST) continue;
				double ga = GRAVITY/(dist*dist);
				double gax = dx/dist * ga;
				double gay = dy/dist * ga;
#ifdef DEBUG
				printf("  anti-gravity by node %d (%s) at %.2f,%.2f: dist=%.2f g=%.2f,%.2f\n",
					j, n2->label, n2->x, n2->y, dist, gax, gay);
#endif
				n->nx += DAMP*gax;
				n->ny += DAMP*gay;
			}
#ifdef DEBUG
			printf("  ends at %.2f,%.2f\n", n->nx, n->ny);
#endif
		}
		/* apply springs */
		for (i=0; i<graph->edges->len; i++) {
			GtkGraphEdge *e = EDGE(graph, i);
#ifdef DEBUG
			printf("Edge %d: %s/%.2f,%.2f -> %s/%.2f,%.2f\n",
				i, e->a->label, e->a->x, e->a->y, e->b->label, e->b->x, e->b->y);
#endif
			double dx = e->a->x - e->b->x;
			double dy = e->a->y - e->b->y;
			double dist = hypot(dx, dy);
			double ga = GRAVITY/(dist*dist);
			double gax = dx/dist * ga;
			double gay = dy/dist * ga;
			double sa = SPRING*(SPRINGLEN - dist);
			double sax = dx/dist * sa;
			double say = dy/dist * sa;
			e->a->nx += DAMP * (sax + gax);
			e->a->ny += DAMP * (say + gay);
			e->b->nx -= DAMP * (sax + gax);
			e->b->ny -= DAMP * (say + gay);
#ifdef DEBUG
			printf("  becomes %.2f,%.2f -> %.2f,%.2f (dist=%.2f a=%.2f,%.2f)\n",
				e->a->nx, e->a->ny, e->b->nx, e->b->ny, dist, sax, say);
#endif
		}
		gboolean moved_one = FALSE;
		for (i=0; i<graph->nodes->len; i++) {
			GtkGraphNode *n = NODE(graph, i);
			if (ABS(n->x-n->nx) + ABS(n->y-n->ny) > MOVE_MIN) {
		//		printf("moved %d %.2f,%.2f\n", i, n->nx-n->x, n->ny-n->y);
				moved_one = TRUE;
			}
			n->x = n->nx;
			n->y = n->ny;
		}
#if 0
		static int q = 0;
		if (++q == 150) {
			gtk_widget_queue_draw(GTK_WIDGET(graph));
			//gtk_main_iteration();
			q=0;
		}
#endif

		if (!moved_one) break;
	}

	/* normalize */
	double xmin = NODE(graph, 0)->x;
	double ymin = NODE(graph, 0)->y;
	double xmax = xmin;
	double ymax = ymin;
	for (i=1; i<graph->nodes->len; i++) {
		GtkGraphNode *n = NODE(graph, i);
		if (n->x < xmin) xmin = n->x;
		if (n->y < ymin) ymin = n->y;
		if (n->x > xmin) xmax = n->x;
		if (n->y > ymin) ymax = n->y;
	}
	for (i=1; i<graph->nodes->len; i++) {
		GtkGraphNode *n = NODE(graph, i);
		n->x -= xmin - 25;
		n->y -= ymin - 12;
	}

	graph->frozen = was_frozen;
}

static void gtk_graph_realize(GtkWidget *widget) {
  GtkGraph *graph;
  GdkWindowAttr attributes;
  gint attributes_mask;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(GTK_IS_GRAPH(widget));
  
  graph = GTK_GRAPH(widget);
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
  gdk_window_set_user_data(widget->window, graph);
  
  widget->style = gtk_style_attach(widget->style, widget->window);
  gtk_style_set_background(widget->style, widget->window, GTK_STATE_ACTIVE);
}

static void gtk_graph_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
  g_return_if_fail(widget != NULL);
  g_return_if_fail(GTK_IS_GRAPH(widget));
  g_return_if_fail(allocation != NULL);

  widget->allocation = *allocation;
  
  if (GTK_WIDGET_REALIZED(widget))
    gdk_window_move_resize(widget->window,
      allocation->x, allocation->y, allocation->width, allocation->height);
}

#define X(_x) (_x)
#define Y(_y) (_y)

static gint gtk_graph_expose(GtkWidget *widget, GdkEventExpose *ev) {
	GtkGraph *graph;
	GdkGC *gc;
	GdkPixmap *drawbuffer;
	int i;
	static GdkColor black = {0};
	static GdkColor white = {0};
	static GdkColor color[] = {
//		{ 0, 0xff00, 0x0000, 0x0000 },
	};
	static int ncolors = sizeof(color)/sizeof(color[0]);
	PangoLayout *layout;

  g_return_val_if_fail(widget != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_GRAPH(widget), FALSE);
  g_return_val_if_fail(ev != NULL, FALSE);
	g_return_val_if_fail(GTK_WIDGET_DRAWABLE(widget), FALSE);

	graph = GTK_GRAPH(widget);
//	g_return_val_if_fail(!graph->frozen, FALSE);

	if (white.pixel == 0) {
		GdkColormap *cmap = gdk_window_get_colormap(widget->window);
		gdk_color_black(cmap, &black);
		gdk_color_white(cmap, &white);
		for (i=0; i<ncolors; i++)
			gdk_color_alloc(cmap, &color[i]);
	}

	if (graph->nodes->len == 0) {
		g_assert(graph->edges->len == 0);
		gc = gdk_gc_new(widget->window);
		gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->bg[GTK_STATE_NORMAL]);
		gdk_draw_rectangle(widget->window, gc, TRUE, ev->area.x, ev->area.y,
			ev->area.width, ev->area.height);
		gdk_gc_destroy(gc);
		return TRUE;
	}

	drawbuffer = gdk_pixmap_new(widget->window,
		ev->area.width, ev->area.height, -1);
	gc = gdk_gc_new(drawbuffer);
	gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->bg[GTK_STATE_NORMAL]);
	gdk_draw_rectangle(drawbuffer, gc, TRUE, 0, 0,
		ev->area.width, ev->area.height);
	gdk_gc_set_foreground(gc, &black);

	/* draw edges */
	for (i=0; i<graph->edges->len; i++) {
		const GtkGraphEdge *e = EDGE(graph, i);
		//printf("Edge %d: %s/%.2f,%.2f - %s/%.2f,%.2f (%s)\n",
			//i, e->a->label, e->a->x, e->a->y, e->b->label, e->b->x, e->b->y,
			//e->directed ? "directed" : "undirected");
		//!! if directed, draw the arrow
		if (e->directed) {
			double len = hypot(X(e->a->x)-X(e->b->x), Y(e->a->y)-Y(e->b->y));
			double px = X(e->b->x) - (X(e->b->x) - X(e->a->x)) * ARROW_SIZE/len;
			double py = Y(e->b->y) - (Y(e->b->y) - Y(e->a->y)) * ARROW_SIZE/len;
			double mx = (X(e->b->x) - X(e->a->x)) / len;
			double my = (Y(e->b->y) - Y(e->a->y)) / len;
			double e1x = px + ARROW_WIDTH * my;
			double e1y = py - ARROW_WIDTH * mx;
			double e2x = px - ARROW_WIDTH * my;
			double e2y = py + ARROW_WIDTH * mx;
			gdk_draw_line(drawbuffer, gc, X(e->a->x)-ev->area.x, Y(e->a->y)-ev->area.y, px-ev->area.x, py-ev->area.y);
			gdk_draw_line(drawbuffer, gc, e1x-ev->area.x, e1y-ev->area.y, e2x-ev->area.x, e2y-ev->area.y);
			gdk_draw_line(drawbuffer, gc, e1x-ev->area.x, e1y-ev->area.y, X(e->b->x)-ev->area.x, Y(e->b->y)-ev->area.y);
			gdk_draw_line(drawbuffer, gc, e2x-ev->area.x, e2y-ev->area.y, X(e->b->x)-ev->area.x, Y(e->b->y)-ev->area.y);
		}
		else
			gdk_draw_line(drawbuffer, gc, X(e->a->x)-ev->area.x, Y(e->a->y)-ev->area.y, X(e->b->x)-ev->area.x, Y(e->b->y)-ev->area.y);
	}

	/* draw nodes */
	layout = gtk_widget_create_pango_layout(widget, "0");
	for (i=0; i<graph->nodes->len; i++) {
		const GtkGraphNode *n = NODE(graph, i);
		int width, height;

		//printf("Node %d: \"%s\" %.2f,%.2f\n", i, n->label, n->x, n->y);

		if (n->label) {

			pango_layout_set_text(layout, n->label, -1);
			pango_layout_get_size(layout, &width, &height);
			width /= PANGO_SCALE;
			height /= PANGO_SCALE;
			//printf("  \"%s\" is %d x %d\n", n->label, width, height);

			/* draw the node empty */
			gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->bg[GTK_STATE_NORMAL]);
			gdk_draw_rectangle(drawbuffer, gc, TRUE,
				X(n->x) - width/2 - X_MARGIN-ev->area.x, Y(n->y) - height/2 - Y_MARGIN-ev->area.y,
				width + 2*X_MARGIN, height + 2*X_MARGIN);
			/* now draw the text and the outline */
			gdk_gc_set_foreground(gc, &black);
			gdk_draw_layout(drawbuffer, gc, X(n->x) - width/2-ev->area.x, Y(n->y) - height/2-ev->area.y, layout);
			gdk_draw_rectangle(drawbuffer, gc, FALSE,
				X(n->x) - width/2 - X_MARGIN-ev->area.x, Y(n->y) - height/2 - Y_MARGIN-ev->area.y,
				width + 2*X_MARGIN, height + 2*X_MARGIN);
		}
		else
			gdk_draw_rectangle(drawbuffer, gc, FALSE, X(n->x) - 10-ev->area.x, Y(n->y) - 4-ev->area.y, 20, 8);
	}
	g_object_unref(layout);

	gdk_gc_destroy(gc);

	gc = gdk_gc_new(widget->window);
	gdk_draw_pixmap(widget->window, gc, drawbuffer, 0, 0,
		ev->area.x, ev->area.y, ev->area.width, ev->area.height);
	gdk_pixmap_unref(drawbuffer);
	gdk_gc_destroy(gc);

  return TRUE;
}

static GtkGraphNode *node_dragging = NULL;

static gint gtk_graph_motion(GtkWidget *widget, GdkEventMotion *ev) {
	if (!node_dragging) return FALSE;

	node_dragging->x = ev->x;
	node_dragging->y = ev->y;
	gtk_widget_queue_draw(widget);

	return TRUE;
}

static gint gtk_graph_button_press(GtkWidget *widget, GdkEventButton *ev) {
	GtkGraph *graph;
	int i;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_GRAPH(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	graph = GTK_GRAPH(widget);
	if (graph->frozen) return FALSE;

	g_assert(node_dragging == NULL);
	for (i=0; i<graph->nodes->len; i++) {
		GtkGraphNode *node = NODE(graph, i);
		if (ABS(ev->x - node->x) + ABS(ev->y - node->y) < 30) {
			node_dragging = node;
			break;
		}
	}
	if (!node_dragging) return FALSE;

	g_signal_emit(G_OBJECT(graph), graph_signals[NODE_CLICKED], 0, node_dragging);

	gtk_widget_queue_draw(widget);

	return TRUE;
}

static gint gtk_graph_button_release(GtkWidget *widget, GdkEventButton *ev) {
	if (!node_dragging) return FALSE;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_GRAPH(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	GtkGraph *graph = GTK_GRAPH(widget);

	node_dragging = NULL;
	if (!graph->frozen) gtk_graph_update(graph);
	return TRUE;
}
