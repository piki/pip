/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtksignal.h>

#include "pathtl.h"
#include "common.h"

#define Y_GAP_THREAD 5      // space between two threads
#define Y_GAP_TASK 3        // space between task and subtask
#define Y_TASK_WIDTH 10     // vertical width of a task
#define MARGIN 5          
#define MIN_TASK_SIZE 3     // minimum number of pixels of duration a task must be
#define SCALE_MIN_LEN 70    // must be <= 1/10 allocation.width
#define SCALE_SPACE 10      // pixels allotted to the scale
#define SCALE_TIC_SIZE 4    // height in pixels of the scale tics

enum {
	NODE_CLICKED,
	NODE_ACTIVATED,
	ZOOM_CHANGED,
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
static gint gtk_pathtl_button_release (GtkWidget         *widget,
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
			G_TYPE_NONE, 1, G_TYPE_POINTER);

	pathtl_signals[NODE_ACTIVATED] =
		g_signal_new("node_activated",
			G_TYPE_FROM_CLASS(klass),
			(GSignalFlags)(G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION),
			G_STRUCT_OFFSET(GtkPathTLClass, node_activated),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE, 1, G_TYPE_POINTER);

	pathtl_signals[ZOOM_CHANGED] =
		g_signal_new("zoom_changed",
			G_TYPE_FROM_CLASS(klass),
			(GSignalFlags)(G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION),
			G_STRUCT_OFFSET(GtkPathTLClass, zoom_changed),
			NULL, NULL,
			g_cclosure_marshal_VOID__DOUBLE,
			G_TYPE_NONE, 1, G_TYPE_DOUBLE);

	widget_class->realize = gtk_pathtl_realize;
	widget_class->size_allocate = gtk_pathtl_size_allocate;
	widget_class->expose_event = gtk_pathtl_expose;
	widget_class->button_press_event = gtk_pathtl_button_press;
	widget_class->button_release_event = gtk_pathtl_button_release;
	
	/*class->value_changed = NULL;*/
}

static void gtk_pathtl_init(GtkPathTL *pathtl) {
	pathtl->path = NULL;
	pathtl->zoom = 0.001;  // pixels per microsecond
	pathtl->flags = PATHTL_DEFAULTS;
	pathtl->where_clicked = NULL;

	gtk_widget_set_events(GTK_WIDGET(pathtl),
		GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
}

GtkWidget *gtk_pathtl_new(const struct timeval &trace_start) {
	GtkWidget *pathtl;

	pathtl = (GtkWidget*)gtk_type_new(gtk_pathtl_get_type());
	pathtl->requisition.width = 20;
	pathtl->requisition.height = 20;
	GTK_PATHTL(pathtl)->trace_start = trace_start;

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

void gtk_pathtl_set_zoom(GtkPathTL *pathtl, double zoom) {
	g_return_if_fail(pathtl);
	g_return_if_fail(GTK_IS_PATHTL(pathtl));
	if (fabs(pathtl->zoom - zoom) < 0.000001) return;
	pathtl->zoom = zoom;
	gtk_widget_set_size_request(GTK_WIDGET(pathtl), (int)(pathtl->maxx*zoom) + 2*MARGIN + SCALE_SPACE, pathtl->height);
	gtk_widget_queue_draw(GTK_WIDGET(pathtl));
	g_signal_emit(G_OBJECT(pathtl), pathtl_signals[ZOOM_CHANGED], 0, pathtl->zoom);
}

void gtk_pathtl_set_flags(GtkPathTL *pathtl, GtkPathtlFlags flags) {
	g_return_if_fail(pathtl);
	g_return_if_fail(GTK_IS_PATHTL(pathtl));
	if (flags == pathtl->flags) return;
	if ((flags ^ pathtl->flags) & PATHTL_SHOW_SUBTASKS) {  // did we change "show subtasks?"
		pathtl->flags = flags;
		gtk_pathtl_layout(pathtl);
	}
	else
		pathtl->flags = flags;
	gtk_widget_queue_draw(GTK_WIDGET(pathtl));
}

void gtk_pathtl_set_trace_start(GtkPathTL *pathtl, const struct timeval &trace_start) {
	g_return_if_fail(pathtl);
	g_return_if_fail(GTK_IS_PATHTL(pathtl));
	pathtl->trace_start = trace_start;
	gtk_widget_queue_draw(GTK_WIDGET(pathtl));
}

//!! these really should be per pathtl object, but there's only ever one
//pathtl object, so we get away with it
struct LayoutElem {
	LayoutElem(PathEvent *_ev, int _x, int _y, int _color)
			: ev(_ev), x(_x), y(_y), x2(_x), y2(_y), color(_color) {}
	LayoutElem(PathEvent *_ev, int _x, int _y, int _width, int _color)
			: ev(_ev), x(_x), y(_y), x2(_x+_width), y2(_y), color(_color) {}
	LayoutElem(PathEvent *_ev, int _x, int _y, int _x2, int _y2, int _color)
			: ev(_ev), x(_x), y(_y), x2(_x2), y2(_y2), color(_color) {}
	PathEvent *ev;
	int x, y, x2, y2, color;
};
static std::vector<LayoutElem> layout;
static std::map<const PathEvent*, int> messages;

static int layout_list(const PathEventList &list, const timeval &start, int y, GtkPathTL *pathtl, int color, int depth) {
	int newy, bottom=y+Y_TASK_WIDTH;
	for (unsigned int i=0; i<list.size(); i++) {
		int begin = list[i]->start() - start;
		int end = list[i]->end() - start;
		if (end > pathtl->maxx) pathtl->maxx = end;
		int adj = (depth == 0 || !(pathtl->flags & PATHTL_SHOW_SUBTASKS)) ? Y_TASK_WIDTH/2 : -(Y_TASK_WIDTH+Y_GAP_TASK)/2;
#if 0
		printf("draw: type=%d start=%ld.%06ld end=%ld.%06ld y=%d\n",
			list[i]->type(), list[i]->start().tv_sec, list[i]->start().tv_usec,
			list[i]->end().tv_sec, list[i]->end().tv_usec, y);
#endif
		switch (list[i]->type()) {
			case PEV_TASK:{
				const PathTask *task = dynamic_cast<const PathTask*>(list[i]);
				if ((pathtl->flags & PATHTL_SHOW_SUBTASKS) || depth == 0)
					layout.push_back(LayoutElem(list[i], begin, y, end-begin, color));
				if ((pathtl->flags & PATHTL_SHOW_SUBTASKS) && !task->children.empty()) {
					newy = y + Y_TASK_WIDTH + Y_GAP_TASK
							+ layout_list(task->children, start, y+Y_TASK_WIDTH+Y_GAP_TASK, pathtl, color, depth+1);
					if (newy > bottom) bottom = newy;
				}
				else
					(void)layout_list(task->children, start, y, pathtl, color, depth+1);
				break;
				}
			case PEV_NOTICE:
				layout.push_back(LayoutElem(list[i], begin, y+adj, color));
				break;
			case PEV_MESSAGE_SEND:{
				const PathMessageSend *send = dynamic_cast<const PathMessageSend*>(list[i]);
				std::map<const PathEvent*, int>::const_iterator recvp = messages.find(send->recv);
				if (recvp != messages.end()) {
					layout.push_back(LayoutElem(list[i], begin, y+adj, send->recv->start()-start, recvp->second, color));
					messages.erase(send->recv);
				}
				else
					messages[list[i]] = y+adj;
				break;
				}
			case PEV_MESSAGE_RECV:{
				const PathMessageRecv *recv = dynamic_cast<const PathMessageRecv*>(list[i]);
				std::map<const PathEvent*, int>::const_iterator sendp = messages.find(recv->send);
				if (sendp != messages.end()) {
					layout.push_back(LayoutElem(list[i], recv->send->start()-start, sendp->second, begin, y+adj, color));
					messages.erase(recv->send);
				}
				else
					messages[list[i]] = y+adj;
				break;
				}
			default:
				fprintf(stderr, "Invalid event type %d\n", list[i]->type());
				exit(1);
		}
	}

	return bottom-y;
}

static void gtk_pathtl_layout(GtkPathTL *pathtl) {
	g_return_if_fail(pathtl);
	g_return_if_fail(pathtl->path);

	int y = MARGIN + SCALE_SPACE;
	layout.clear();

	pathtl->maxx = 0;

	const PathEventList *root = &pathtl->path->thread_pools.find(pathtl->path->root_thread)->second;
	assert(!root->empty());
	// root thread goes first
	y += layout_list(*root, (*root)[0]->start(), y, pathtl, 0, 0) + Y_GAP_THREAD;

	std::map<int,PathEventList>::const_iterator threadp;
	int color = 1;
	for (threadp=pathtl->path->thread_pools.begin();
			threadp!=pathtl->path->thread_pools.end();
			threadp++) {
		if (threadp->first == pathtl->path->root_thread) continue;  // handled above
		if (!threadp->second.empty())
			y += layout_list(threadp->second, (*root)[0]->start(), y, pathtl, color++, 0) + Y_GAP_THREAD;
	}

	gtk_pathtl_set_zoom(pathtl, (double)(GTK_WIDGET(pathtl)->allocation.width - 2*MARGIN) / pathtl->maxx);
	pathtl->height = y-Y_GAP_THREAD+2*MARGIN+SCALE_SPACE;
	gtk_widget_set_size_request(GTK_WIDGET(pathtl), -1, pathtl->height);
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

#define X1(_x) (MARGIN + (int)(pathtl->zoom * (_x)))
#define Y1(_y) (SCALE_SPACE + MARGIN + (_y))

#define X(_x) (X1(_x) - ev->area.x)
#define Y(_y) (Y1(_y) - ev->area.y)

static gint gtk_pathtl_expose(GtkWidget *widget, GdkEventExpose *ev) {
	GtkPathTL *pathtl;
	GdkGC *gc;
	GdkPixmap *drawbuffer;

	static GdkColor color[] = {
		{ 0, 0xff00, 0x0000, 0x0000 },
		{ 0, 0x0000, 0x9900, 0x0000 },
		{ 0, 0x0000, 0x0000, 0xff00 },
		{ 0, 0xcc00, 0x0000, 0xcc00 },
		{ 0, 0x3300, 0xaa00, 0xaa00 },
		{ 0, 0x9900, 0x6600, 0x0000 },
		{ 0, 0x0000, 0x0000, 0x0000 },
		{ 0, 0xcc00, 0x6600, 0x0000 }
	};
	static unsigned int ncolors = sizeof(color)/sizeof(color[0]);
	unsigned int i;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(GTK_IS_PATHTL(widget), FALSE);
	g_return_val_if_fail(ev != NULL, FALSE);
	g_return_val_if_fail(GTK_WIDGET_DRAWABLE(widget), FALSE);

	pathtl = GTK_PATHTL(widget);

	if (color[0].pixel == 0) {
		GdkColormap *cmap = gdk_window_get_colormap(widget->window);
		for (i=0; i<ncolors; i++)
			gdk_color_alloc(cmap, &color[i]);
	}

	drawbuffer = gdk_pixmap_new(widget->window,
		ev->area.width, ev->area.height, -1);
	gc = gdk_gc_new(drawbuffer);
	gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->base[GTK_STATE_NORMAL]);
	gdk_draw_rectangle(drawbuffer, gc, TRUE, 0, 0,
		ev->area.width, ev->area.height);
	gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->text[GTK_STATE_NORMAL]);

	// draw the scale
	int scale_usec = 1, scale_len;
	char *scale_buf;
	while (pathtl->zoom * scale_usec < SCALE_MIN_LEN) scale_usec *= 10;
	scale_len = (int)(pathtl->zoom * scale_usec);

	PangoLayout *playout = gtk_widget_create_pango_layout(widget, "0 sec");
	// draw one extra before and after so that labels halfway off the screen get drawn
	for (int x=MAX(ev->area.x/scale_len-1, 0); (x-1)*scale_len<ev->area.x+ev->area.width; x++) {
		gdk_draw_line(drawbuffer, gc, (x*scale_len)+MARGIN-ev->area.x, MARGIN-ev->area.y,
			((x+1)*scale_len)+MARGIN+scale_len-ev->area.x, MARGIN-ev->area.y);
		gdk_draw_line(drawbuffer, gc, (x*scale_len)+MARGIN-ev->area.x, MARGIN-SCALE_TIC_SIZE-ev->area.y,
			(x*scale_len)+MARGIN-ev->area.x, MARGIN+SCALE_TIC_SIZE-ev->area.y);
		if (x == 0) scale_buf = g_strdup("0");
			else if (scale_usec >= 1000000) scale_buf = g_strdup_printf("%d s", x*scale_usec/1000000);
			else if (scale_usec >= 1000) scale_buf = g_strdup_printf("%d ms", x*scale_usec/1000);
			else scale_buf = g_strdup_printf("%d us", x*scale_usec);
		pango_layout_set_text(playout, scale_buf, -1);
		int width, height;
		pango_layout_get_size(playout, &width, &height);
		width /= PANGO_SCALE;  height /= PANGO_SCALE;
		gdk_draw_layout(drawbuffer, gc, (x*scale_len)-width/2+MARGIN-ev->area.x,
			Y_GAP_TASK+2*SCALE_TIC_SIZE-ev->area.y, playout);
		g_free(scale_buf);
	}
	g_object_unref(G_OBJECT(playout));

	//g_object_unref(layout);

	for (i=0; i<layout.size(); i++) {
		int x = X(layout[i].x), y = Y(layout[i].y), x2 = X(layout[i].x2), y2 = Y(layout[i].y2);
		switch (layout[i].ev->type()) {
			case PEV_TASK:
				if (x2 < x+MIN_TASK_SIZE) x2 = x+MIN_TASK_SIZE;
				if (x >= ev->area.width || x2 < 0 || y >= ev->area.height || y+Y_TASK_WIDTH < 0) continue;
				if (pathtl->where_clicked == layout[i].ev)
					gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->base[GTK_STATE_SELECTED]);
				else
					gdk_gc_set_foreground(gc, &color[layout[i].color % ncolors]);
				gdk_draw_rectangle(drawbuffer, gc, TRUE, x, y, x2-x, Y_TASK_WIDTH);
				gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->text[GTK_STATE_NORMAL]);
				break;
			case PEV_NOTICE:
				if (x-1 >= ev->area.width || x+1 < 0 || y-1 >= ev->area.height || y+1 < 0) continue;
				if (pathtl->flags & PATHTL_SHOW_NOTICES)
					gdk_draw_rectangle(drawbuffer, gc, TRUE, x-1, y-1, 3, 3);
				break;
			case PEV_MESSAGE_SEND:
			case PEV_MESSAGE_RECV:{
				int minx = MIN(x, x2), maxx = MAX(x, x2), miny = MIN(y, y2), maxy = MAX(y, y2);
				if (minx >= ev->area.width || maxx < 0 || miny >= ev->area.height || maxy < 0) continue;
				if (pathtl->flags & PATHTL_SHOW_MESSAGES)
					gdk_draw_line(drawbuffer, gc, x, y, x2, y2);
				}
				break;
			default:
				g_assert_not_reached();
		}
	}

	gdk_gc_destroy(gc);

	gc = gdk_gc_new(widget->window);
	gdk_draw_pixmap(widget->window, gc, drawbuffer, 0, 0,
		ev->area.x, ev->area.y, ev->area.width, ev->area.height);
	gdk_pixmap_unref(drawbuffer);
	gdk_gc_destroy(gc);

	return TRUE;
}

static const PathEvent *find_click(GtkPathTL *pathtl, int x, int y) {
	for (unsigned int i=0; i<layout.size(); i++) {
		int ox, ox2, oy, oy2;
		switch (layout[i].ev->type()) {
			case PEV_TASK:
				ox = X1(layout[i].x);
				oy = Y1(layout[i].y);
				ox2 = X1(layout[i].x2);
				oy2 = Y1(layout[i].y2);
				if (ox2 < ox+MIN_TASK_SIZE) ox2 = ox+MIN_TASK_SIZE;
				if (oy2 < oy+Y_TASK_WIDTH) oy2 = oy+Y_TASK_WIDTH;
				break;
			case PEV_NOTICE:
				ox = X1(layout[i].x)-1;
				oy = Y1(layout[i].y)-1;
				ox2 = ox+2;
				oy2 = ox+2;
				break;
			default:
				continue;
		}
		if (x >= ox && x <= ox2 && y >= oy && y <= oy2)
			return layout[i].ev;
	}
	return NULL;
}

static gint gtk_pathtl_button_press(GtkWidget *widget, GdkEventButton *ev) {
	GtkPathTL *pathtl;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_PATHTL(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	pathtl = GTK_PATHTL(widget);

	pathtl->where_clicked = find_click(pathtl, (int)ev->x, (int)ev->y);
	gtk_widget_queue_draw(widget);

	switch (ev->type) {
		case GDK_BUTTON_PRESS:
			pathtl->where_clicked = find_click(pathtl, (int)ev->x, (int)ev->y);
			return pathtl->where_clicked != NULL;
		case GDK_2BUTTON_PRESS:
			if (pathtl->where_clicked) {
				g_signal_emit(G_OBJECT(pathtl), pathtl_signals[NODE_ACTIVATED], 0, pathtl->where_clicked);
				return TRUE;
			}
			else
				return FALSE;
		default:
			return FALSE;
	}

	return TRUE;
}

static gint gtk_pathtl_button_release(GtkWidget *widget, GdkEventButton *ev) {
	GtkPathTL *pathtl;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_PATHTL(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	pathtl = GTK_PATHTL(widget);

	const PathEvent *where = find_click(pathtl, (int)ev->x, (int)ev->y);
	if (where != NULL)
		g_signal_emit(G_OBJECT(pathtl), pathtl_signals[NODE_CLICKED], 0, where);

	return TRUE;
}
