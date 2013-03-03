#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <gtk/gtksignal.h>
#include <librsvg/rsvg.h>

#include "graph.h"

static void gtk_graph_class_init     (GtkGraphClass      *klass);
static void gtk_graph_init           (GtkGraph           *graph);
static void gtk_graph_realize        (GtkWidget         *widget);
static void gtk_graph_size_allocate  (GtkWidget         *widget,
                                      GtkAllocation      *allocation);
static gint gtk_graph_expose         (GtkWidget         *widget,
                                      GdkEventExpose     *ev);

static void gtk_graph_update(GtkGraph *graph);
static void gtk_graph_layout(GtkGraph *graph);

#define NODE(graph, i) ((GtkGraphNode*)g_ptr_array_index((graph)->nodes, (i)))
#define EDGE(graph, i) ((GtkGraphEdge*)g_ptr_array_index((graph)->edges, (i)))
#define DEFAULT_DPI 60

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
	graph->nodes->len = graph->edges->len = 0;

	gtk_graph_update(graph);
}

static void gtk_graph_class_init(GtkGraphClass *class) {
  GtkWidgetClass *widget_class = (GtkWidgetClass*)class;

  widget_class->realize = gtk_graph_realize;
  widget_class->size_allocate = gtk_graph_size_allocate;
  widget_class->expose_event = gtk_graph_expose;
  
  /*class->value_changed = NULL;*/
}

static void gtk_graph_init(GtkGraph *graph) {
	graph->nodes = g_ptr_array_new();
	graph->edges = g_ptr_array_new();
	graph->frozen = graph->needs_layout = FALSE;
	graph->rsvg = NULL;
	graph->pbuf = NULL;
	graph->layout_program = NULL;
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
	if (graph->rsvg) rsvg_handle_free(graph->rsvg);
	if (graph->pbuf) g_object_unref(G_OBJECT(graph->pbuf));
	if (graph->layout_program) g_free(graph->layout_program);
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

static guint g_edge_hash(gconstpointer v) {
	GtkGraphEdge *edge = (GtkGraphEdge*)v;
	return (int)(long)edge->a ^ (int)(long)edge->b;
}

static gint g_edge_equal(gconstpointer a, gconstpointer b) {
	GtkGraphEdge *edgea = (GtkGraphEdge*)a;
	GtkGraphEdge *edgeb = (GtkGraphEdge*)b;
	return edgea->a == edgeb->a && edgea->b == edgeb->b;
}

void gtk_graph_simplify(GtkGraph *graph) {
	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));

	GHashTable *seen = g_hash_table_new(g_edge_hash, g_edge_equal);
	int i;
	for (i=0; i<graph->edges->len; i++) {
		GtkGraphEdge *edge = EDGE(graph, i);
		GtkGraphEdge *old;
		GtkGraphEdge rev = { edge->b, edge->a, FALSE };
		if (g_hash_table_lookup(seen, edge) != NULL) {  /* already have it */
			g_ptr_array_remove_index_fast(graph->edges, i--);
			continue;
		}
		if ((old = g_hash_table_lookup(seen, &rev)) != NULL) {
			old->directed = FALSE;
			g_ptr_array_remove_index_fast(graph->edges, i--);
			continue;
		}
		g_hash_table_insert(seen, edge, edge);
	}
	g_hash_table_destroy(seen);

	gtk_graph_update(graph);
}

GtkGraphNode *gtk_graph_add_node(GtkGraph *graph, const char *label) {
	GtkGraphNode *ret;

	g_return_val_if_fail(graph, NULL);
	g_return_val_if_fail(GTK_IS_GRAPH(graph), NULL);

	ret = g_new(GtkGraphNode, 1);
	ret->label = label ? g_strdup(label) : NULL;

	g_ptr_array_add(graph->nodes, ret);

	gtk_graph_update(graph);

	return ret;
}

GtkGraphEdge *gtk_graph_add_edge(GtkGraph *graph, GtkGraphNode *a, GtkGraphNode *b, gboolean directed) {
	GtkGraphEdge *e;

	g_return_val_if_fail(graph, NULL);
	g_return_val_if_fail(GTK_IS_GRAPH(graph), NULL);
	g_return_val_if_fail(a, NULL);
	g_return_val_if_fail(b, NULL);

	e = g_new(GtkGraphEdge, 1);
	e->a = a;
	e->b = b;
	e->directed = directed;

	g_ptr_array_add(graph->edges, e);

	gtk_graph_update(graph);

	return e;
}

static void gtk_graph_update(GtkGraph *graph) {
	g_return_if_fail(graph);
	g_return_if_fail(GTK_IS_GRAPH(graph));

	if (graph->frozen) return;

	graph->needs_layout = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

static void gtk_graph_layout(GtkGraph *graph) {
	if (graph->nodes->len == 0) return;
	g_return_if_fail(graph->needs_layout);
	graph->needs_layout = FALSE;

	g_assert(!graph->frozen);

	if (graph->rsvg) rsvg_handle_free(graph->rsvg);
	if (graph->pbuf) g_object_unref(G_OBJECT(graph->pbuf));
	graph->rsvg = rsvg_handle_new();
	rsvg_handle_set_dpi(graph->rsvg, DEFAULT_DPI*graph->zoom);

	/* fork+exec dot */
	int wfd[2]={0,0}, rfd[2]={0,0}, pid=0;
	if (pipe(wfd) == -1) { perror("pipe"); goto done; }
	if (pipe(rfd) == -1) { perror("pipe"); goto done; }
	char *cmd = graph->layout_program;
	if (!cmd) cmd = "dot";
	switch ((pid = fork())) {
		case -1:
			perror("fork");
			goto done;
			return;
		case 0:    /* child */
			dup2(wfd[0], 0);
			dup2(rfd[1], 1);
			close(wfd[0]);
			close(wfd[1]);
			close(rfd[0]);
			close(rfd[1]);
			if (execlp(cmd, cmd, "-Tsvg", NULL) == -1) {
				perror(cmd);
				exit(1);
			}
		default:
			close(wfd[0]);
			close(rfd[1]);
	}

	/* write our graph to dot */
	FILE *wfp = NULL, *rfp = NULL;
	int i;
	if ((wfp = fdopen(wfd[1], "w")) == NULL) { perror("fdopen"); goto done; }
	if ((rfp = fdopen(rfd[0], "r")) == NULL) { perror("fdopen"); goto done; }
	GdkColor text_color = gtk_widget_get_style(GTK_WIDGET(graph))->text[GTK_STATE_NORMAL];
	printf("text_color = %d %d %d\n", text_color.red, text_color.green, text_color.blue);

	fprintf(wfp, "digraph world {\n");
	fprintf(wfp, "node [ color=\"#%02x%02x%02x\",fontcolor=\"#%02x%02x%02x\" ];\n",
		text_color.red>>8, text_color.green>>8, text_color.blue>>8,
		text_color.red>>8, text_color.green>>8, text_color.blue>>8);
	fprintf(wfp, "edge [ color=\"#%02x%02x%02x\" ];\n",
		text_color.red>>8, text_color.green>>8, text_color.blue>>8);
	for (i=0; i<graph->nodes->len; i++)
		fprintf(wfp, "  n%lx [ label = \"%s\" ];\n", (long)NODE(graph, i), NODE(graph, i)->label);
	for (i=0; i<graph->edges->len; i++) {
		fprintf(wfp, "  n%lx -> n%lx%s;\n", (long)EDGE(graph, i)->a, (long)EDGE(graph, i)->b,
			EDGE(graph, i)->directed ? "" : " [ arrowhead=none ]");
	}
	fprintf(wfp, "}\n");
	fclose(wfp);  wfp = NULL;

	unsigned char buf[4096];
	int n;
	GError *err;
	while ((n = fread(buf, 1, sizeof(buf), rfp)) > 0)
		rsvg_handle_write(graph->rsvg, buf, n, &err);
	if (n == -1) { perror("fread"); rsvg_handle_close(graph->rsvg, &err); goto done; }
	fclose(rfp);  rfp = NULL;
	if (!rsvg_handle_close(graph->rsvg, &err)) { fprintf(stderr, "Error parsing SVG\n"); goto done; }

	graph->pbuf = rsvg_handle_get_pixbuf(graph->rsvg);
	fprintf(stderr, "SVG is OK: %d x %d\n",
		gdk_pixbuf_get_width(graph->pbuf), gdk_pixbuf_get_height(graph->pbuf));
	gtk_widget_set_usize(GTK_WIDGET(graph), gdk_pixbuf_get_width(graph->pbuf), gdk_pixbuf_get_height(graph->pbuf));

done:
	if (wfp) fclose(wfp);
	if (rfp) fclose(rfp);
	if (wfd[0] > 0) { close(wfd[0]); close(wfd[1]); }
	if (rfd[0] > 0) { close(rfd[0]); close(rfd[1]); }
	int returncode;
	if (pid > 0) waitpid(pid, &returncode, 0);
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

static gint gtk_graph_expose(GtkWidget *widget, GdkEventExpose *ev) {
	GtkGraph *graph;
	GdkGC *gc;

  g_return_val_if_fail(widget != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_GRAPH(widget), FALSE);
  g_return_val_if_fail(ev != NULL, FALSE);
	g_return_val_if_fail(GTK_WIDGET_DRAWABLE(widget), FALSE);

	graph = GTK_GRAPH(widget);
	if (graph->needs_layout) gtk_graph_layout(graph);

	gc = gdk_gc_new(widget->window);
	gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->base[GTK_STATE_NORMAL]);
	gdk_draw_rectangle(widget->window, gc, TRUE, ev->area.x, ev->area.y,
		ev->area.width, ev->area.height);
	if (graph->nodes->len == 0) {
		g_assert(graph->edges->len == 0);
		gdk_gc_destroy(gc);
		return TRUE;
	}

	gdk_draw_pixbuf(widget->window, gc, graph->pbuf, 0,0, 0,0, -1,-1, GDK_RGB_DITHER_NORMAL, 0,0);

	gdk_gc_destroy(gc);

  return TRUE;
}

void gtk_graph_set_layout_program(GtkGraph *graph, const char *prog) {
	g_return_if_fail(graph);
	if (graph->layout_program) g_free(graph->layout_program);
	graph->layout_program = (prog && prog[0]) ? g_strdup(prog) : NULL;
	gtk_graph_update(graph);
}

void gtk_graph_set_zoom(GtkGraph *graph, double zoom) {
	g_return_if_fail(graph);
	g_return_if_fail(zoom >= 0.05);
	g_return_if_fail(zoom <= 10);
	graph->zoom = zoom;
	gtk_graph_update(graph);
}
