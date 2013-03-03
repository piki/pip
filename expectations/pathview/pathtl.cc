#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtksignal.h>

#include "pathtl.h"
#include "common.h"

#define Y_GAP_THREAD 50
#define Y_TASK_WIDTH 10
#define X_TIME_SCALE 300

enum {
  NODE_CLICKED,
  LAST_SIGNAL
};

static guint pathtl_signals[LAST_SIGNAL] = { 0 };

static void gtk_pathtl_class_init     (GtkPathTLClass      *klass);
static void gtk_pathtl_init           (GtkPathTL           *pathtl);
static void gtk_pathtl_realize        (GtkWidget         *widget);
static void gtk_pathtl_size_allocate  (GtkWidget         *widget,
                                      GtkAllocation      *allocation);
static gint gtk_pathtl_expose         (GtkWidget         *widget,
                                      GdkEventExpose     *ev);
static gint gtk_pathtl_button_press   (GtkWidget         *widget,
                                      GdkEventButton     *ev);

static void gtk_pathtl_layout(GtkPathTL *pathtl);

GType gtk_pathtl_get_type() {
  static GType pathtl_type = 0;

  if (!pathtl_type) {
    static const GTypeInfo pathtl_info = {
      sizeof(GtkPathTLClass),
			NULL,
			NULL,
      (GClassInitFunc)gtk_pathtl_class_init,
			NULL,
			NULL,
      sizeof(GtkPathTL),
			0,
      (GInstanceInitFunc)gtk_pathtl_init,
    };
    
    pathtl_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkPathTL", &pathtl_info, (GTypeFlags)0);
  }
  
  return pathtl_type;
}

static void gtk_pathtl_class_init(GtkPathTLClass *klass) {
  GtkWidgetClass *widget_class = (GtkWidgetClass*)klass;

  pathtl_signals[NODE_CLICKED] =
    g_signal_new("node_clicked",
      G_TYPE_FROM_CLASS(klass),
      (GSignalFlags)(G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION),
      G_STRUCT_OFFSET(GtkPathTLClass, node_clicked),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

  widget_class->realize = gtk_pathtl_realize;
  widget_class->size_allocate = gtk_pathtl_size_allocate;
  widget_class->expose_event = gtk_pathtl_expose;
	widget_class->button_press_event = gtk_pathtl_button_press;
  
  /*class->value_changed = NULL;*/
}

static void gtk_pathtl_init(GtkPathTL *pathtl) {
	pathtl->path = NULL;
	pathtl->times_to_scale = TRUE;
}

GtkWidget *gtk_pathtl_new(void) {
  GtkWidget *pathtl;

  pathtl = (GtkWidget*)gtk_type_new(gtk_pathtl_get_type());
  pathtl->requisition.width = 20;
  pathtl->requisition.height = 20;

  return pathtl;
}

void gtk_pathtl_free(GtkPathTL *pathtl) {
	g_free(pathtl);
}

void gtk_pathtl_set(GtkPathTL *pathtl, const Path *path) {
	g_return_if_fail(pathtl);
	g_return_if_fail(GTK_IS_PATHTL(pathtl));
	pathtl->path = path;
	gtk_pathtl_layout(pathtl);
	gtk_widget_queue_draw(GTK_WIDGET(pathtl));
}

void gtk_pathtl_set_times_to_scale(GtkPathTL *pathtl, gboolean scale) {
	g_return_if_fail(pathtl);
	g_return_if_fail(GTK_IS_PATHTL(pathtl));
	if (scale == pathtl->times_to_scale) return;
	pathtl->times_to_scale = scale;
	gtk_pathtl_layout(pathtl);
	gtk_widget_queue_draw(GTK_WIDGET(pathtl));
}

static void gtk_pathtl_layout(GtkPathTL *pathtl) {
}

static void gtk_pathtl_realize(GtkWidget *widget) {
  GtkPathTL *pathtl;
  GdkWindowAttr attributes;
  gint attributes_mask;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(GTK_IS_PATHTL(widget));
  
  pathtl = GTK_PATHTL(widget);
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
  gdk_window_set_user_data(widget->window, pathtl);
  
  widget->style = gtk_style_attach(widget->style, widget->window);
  gtk_style_set_background(widget->style, widget->window, GTK_STATE_ACTIVE);
}

static void gtk_pathtl_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
  g_return_if_fail(widget != NULL);
  g_return_if_fail(GTK_IS_PATHTL(widget));
  g_return_if_fail(allocation != NULL);

  widget->allocation = *allocation;
  
  if (GTK_WIDGET_REALIZED(widget))
    gdk_window_move_resize(widget->window,
      allocation->x, allocation->y, allocation->width, allocation->height);
}

#define X(_x) (_x)
#define Y(_y) (_y)

static void draw_list(GtkPathTL *pathtl, GdkPixmap *drawbuffer, GdkGC *gc,
		const PathEventList &list, const timeval &start, int y) {
	for (unsigned int i=0; i<list.size(); i++) {
		int x = (list[i]->start() - start) / X_TIME_SCALE;
		printf("draw: type=%d start=%ld.%06ld end=%ld.%06ld y=%d\n",
			list[i]->type(), list[i]->start().tv_sec, list[i]->start().tv_usec,
			list[i]->end().tv_sec, list[i]->end().tv_usec, y);
		switch (list[i]->type()) {
			case PEV_TASK:
				gdk_draw_line(drawbuffer, gc, x, y, x, y+Y_TASK_WIDTH-1);
				gdk_draw_rectangle(drawbuffer, gc, FALSE, x, y+2,
					(list[i]->end()-start)/X_TIME_SCALE, Y_TASK_WIDTH-5);
				draw_list(pathtl, drawbuffer, gc, ((PathTask*)list[i])->children,
					start, y+Y_TASK_WIDTH+1);
				break;
			case PEV_NOTICE:
				gdk_draw_rectangle(drawbuffer, gc, TRUE, x-1, y+Y_TASK_WIDTH/2-1, 3, 3);
				break;
			case PEV_MESSAGE_SEND:
			case PEV_MESSAGE_RECV:
				break;
			default:
				fprintf(stderr, "Invalid event type %d\n", list[i]->type());
				exit(1);
		}
	}
}

static gint gtk_pathtl_expose(GtkWidget *widget, GdkEventExpose *ev) {
	GtkPathTL *pathtl;
	GdkGC *gc;
	GdkPixmap *drawbuffer;
	int i;
	static GdkColor black = {0};
	static GdkColor white = {0};
	static GdkColor color[] = {
//		{ 0, 0xff00, 0x0000, 0x0000 },
	};
	static int ncolors = sizeof(color)/sizeof(color[0]);

  g_return_val_if_fail(widget != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_PATHTL(widget), FALSE);
  g_return_val_if_fail(ev != NULL, FALSE);
	g_return_val_if_fail(GTK_WIDGET_DRAWABLE(widget), FALSE);

	pathtl = GTK_PATHTL(widget);
//	g_return_val_if_fail(!pathtl->frozen, FALSE);

	if (white.pixel == 0) {
		GdkColormap *cmap = gdk_window_get_colormap(widget->window);
		gdk_color_black(cmap, &black);
		gdk_color_white(cmap, &white);
		for (i=0; i<ncolors; i++)
			gdk_color_alloc(cmap, &color[i]);
	}

	drawbuffer = gdk_pixmap_new(widget->window,
		ev->area.width, ev->area.height, -1);
	gc = gdk_gc_new(drawbuffer);
	gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->bg[GTK_STATE_NORMAL]);
	gdk_draw_rectangle(drawbuffer, gc, TRUE, 0, 0,
		ev->area.width, ev->area.height);
	gdk_gc_set_foreground(gc, &black);

	std::map<int,PathEventList>::const_iterator childp;
	int y = 0;
	if (!pathtl->path) goto nopath;
	for (childp=pathtl->path->children.begin();
			childp!=pathtl->path->children.end();
			childp++) {
		if (childp->second.size() != 0)
			draw_list(pathtl, drawbuffer, gc, childp->second,
				childp->second[0]->start(), y);
		y += Y_GAP_THREAD;
	}

	//g_object_unref(layout);

nopath:
	gdk_gc_destroy(gc);

	gc = gdk_gc_new(widget->window);
	gdk_draw_pixmap(widget->window, gc, drawbuffer, 0, 0,
		ev->area.x, ev->area.y, ev->area.width, ev->area.height);
	gdk_pixmap_unref(drawbuffer);
	gdk_gc_destroy(gc);

  return TRUE;
}

static gint gtk_pathtl_button_press(GtkWidget *widget, GdkEventButton *ev) {
	//GtkPathTL *pathtl;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_PATHTL(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	//pathtl = GTK_PATHTL(widget);

	printf("button pressed: %.2f, %.2f\n", ev->x, ev->y);

	return TRUE;
}
