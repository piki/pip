#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtksignal.h>

#include "plot.h"

#define MARGIN 5
#define TEXT_GAP_X 5
#define TEXT_GAP_Y 5

enum {
	POINT_CLICKED,
	LAST_SIGNAL
};

static guint plot_signals[LAST_SIGNAL] = { 0 };

typedef struct _GtkPlotLine GtkPlotLine;
struct _GtkPlotLine {
	GArray *points;
};

static void gtk_plot_class_init     (GtkPlotClass   *klass);
static void gtk_plot_init           (GtkPlot        *plot);
static void gtk_plot_realize        (GtkWidget      *widget);
static void gtk_plot_size_allocate  (GtkWidget      *widget,
                                     GtkAllocation  *allocation);
static gint gtk_plot_expose         (GtkWidget      *widget,
                                     GdkEventExpose *ev);
static gint gtk_plot_button_press   (GtkWidget      *widget,
                                     GdkEventButton *ev);

static void gtk_plot_update(GtkPlot *plot);
static void gtk_plot_layout(GtkPlot *plot);
static int get_precision(double n);
static void gtk_plot_set_highlight(GtkPlot *plot, int click_x, int click_y);

#define LINE(plot, i) g_array_index((plot)->lines, GtkPlotLine, i)
#define CURRENT_LINE(plot) LINE(plot, (plot)->lines->len-1)
#define POINT(plot, i, j) g_array_index(LINE(plot, i).points, GtkPlotPoint, (j))

GType gtk_plot_get_type(void) {
	static GType plot_type = 0;

	if (!plot_type) {
		static const GTypeInfo plot_info = {
			sizeof(GtkPlotClass),
			NULL,
			NULL,
			(GClassInitFunc)gtk_plot_class_init,
			NULL,
			NULL,
			sizeof(GtkPlot),
			0,
			(GInstanceInitFunc)gtk_plot_init,
		};
		
		plot_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkPlot", &plot_info, 0);
	}
	
	return plot_type;
}

void gtk_plot_clear(GtkPlot *plot) {
	int i;

	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));

	for (i=0; i<plot->lines->len; i++)
		g_array_free(LINE(plot, i).points, TRUE);
	g_array_free(plot->lines, TRUE);
	plot->lines = g_array_new(FALSE, FALSE, sizeof(GtkPlotLine));

	if (!plot->frozen) gtk_plot_update(plot);
}

static void gtk_plot_class_init(GtkPlotClass *class) {
	GtkWidgetClass *widget_class = (GtkWidgetClass*)class;

	plot_signals[POINT_CLICKED] =
		g_signal_new("point_clicked",
			G_TYPE_FROM_CLASS(class),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET(GtkPlotClass, point_clicked),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

	widget_class->realize = gtk_plot_realize;
	widget_class->size_allocate = gtk_plot_size_allocate;
	widget_class->expose_event = gtk_plot_expose;
	widget_class->button_press_event = gtk_plot_button_press;
	
	/*class->value_changed = NULL;*/
}

static void gtk_plot_init(GtkPlot *plot) {
	plot->lines = g_array_new(FALSE, FALSE, sizeof(GtkPlotLine));
	plot->frozen = FALSE;
	plot->highlight_line = plot->highlight_point = -1;

	gtk_widget_set_events(GTK_WIDGET(plot), GDK_BUTTON_PRESS_MASK);
}

GtkWidget *gtk_plot_new(GtkPlotFlags flags) {
	GtkWidget *plot;

	plot = gtk_type_new(gtk_plot_get_type());
	plot->requisition.width = 160;
	plot->requisition.height = 120;
	GTK_PLOT(plot)->flags = flags;

	return plot;
}

void gtk_plot_free(GtkPlot *plot) {
	int i;

	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));

	for (i=0; i<plot->lines->len; i++)
		g_array_free(LINE(plot, i).points, TRUE);
	g_array_free(plot->lines, TRUE);
	g_free(plot);
}

void gtk_plot_freeze(GtkPlot *plot) {
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));

	plot->frozen = TRUE;
}

void gtk_plot_thaw(GtkPlot *plot) {
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));

	plot->frozen = FALSE;
	gtk_plot_update(plot);
	gtk_widget_queue_draw(GTK_WIDGET(plot));
}

void gtk_plot_add_point(GtkPlot *plot, double x, double y) {
	GtkPlotPoint p = { x, y };

	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));
	g_return_if_fail(plot->lines->len > 0);

	g_array_append_val(CURRENT_LINE(plot).points, p);

	if (!plot->frozen) gtk_plot_update(plot);
}

static void gtk_plot_update(GtkPlot *plot) {
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));
	g_return_if_fail(!plot->frozen);

	plot->highlight_line = plot->highlight_point = -1;
	gtk_plot_layout(plot);
	gtk_widget_queue_draw(GTK_WIDGET(plot));
}

static double pick_tic(double range) {
	return pow(10, floor(log10(range)));
}

static void gtk_plot_layout(GtkPlot *plot) {
	PangoLayout *layout;
	char buf[64];
	int width, height;

	if (plot->lines->len == 0) return;

	/* find min and max x and y */
	if (plot->flags & (PLOT_AUTO_X|PLOT_AUTO_Y)) {
		int i, j;
		GtkPlotPoint p = POINT(plot, 0, 0);
		if (plot->flags & PLOT_LOGSCALE_X) p.x = log10(p.x);
		if (plot->flags & PLOT_LOGSCALE_Y) p.y = log10(p.y);
		if (plot->flags & PLOT_AUTO_X) plot->xmin = plot->xmax = p.x;
		if (plot->flags & PLOT_AUTO_Y) plot->ymin = plot->ymax = p.y;
		for (i=0; i<plot->lines->len; i++) {
			for (j=0; j<LINE(plot, i).points->len; j++) {
				p = POINT(plot, i, j);
				if (plot->flags & PLOT_LOGSCALE_X) p.x = log10(p.x);
				if (plot->flags & PLOT_LOGSCALE_Y) p.y = log10(p.y);
				if (plot->flags & PLOT_AUTO_X) {
					if (p.x < plot->xmin) plot->xmin = p.x;
					if (p.x > plot->xmax) plot->xmax = p.x;
				}
				if (plot->flags & PLOT_AUTO_Y) {
					if (p.y < plot->ymin) plot->ymin = p.y;
					if (p.y > plot->ymax) plot->ymax = p.y;
				}
			}
		}
		if (plot->flags & PLOT_AUTO_X && plot->xmin == plot->xmax)
			plot->xmin--, plot->xmax++;
		if (plot->flags & PLOT_AUTO_Y && plot->ymin == plot->ymax)
			plot->ymin--, plot->ymax++;
	}
	if (plot->flags & PLOT_X0) plot->xmin = 0;
	if (plot->flags & PLOT_Y0) plot->ymin = 0;

	/* pick tics */
	if (plot->flags & PLOT_LOGSCALE_X)
		plot->xtic = 1;
	else
		plot->xtic = pick_tic(plot->xmax - plot->xmin);
	if (plot->flags & PLOT_LOGSCALE_Y)
		plot->ytic = 1;
	else
		plot->ytic = pick_tic(plot->ymax - plot->ymin);

	/* bump min/max to tics */
	if (plot->flags & PLOT_AUTO_X) {
		plot->xmin = plot->xtic * floor(plot->xmin / plot->xtic);
		plot->xmax = plot->xtic * ceil(plot->xmax / plot->xtic);
	}
	if (plot->flags & PLOT_AUTO_Y) {
		plot->ymin = plot->ytic * floor(plot->ymin / plot->ytic);
		plot->ymax = plot->ytic * ceil(plot->ymax / plot->ytic);
	}

	/* find size of largest X/Y labels; set margins */
	if (plot->flags & PLOT_LOGSCALE_Y)
		sprintf(buf, "%.*f", MAX(-(int)plot->ymax, 0), pow(10, plot->ymax));
	else
		sprintf(buf, "%.*f", get_precision(plot->ymax), plot->ymax);
	layout = gtk_widget_create_pango_layout(GTK_WIDGET(plot), buf);
	pango_layout_get_size(layout, &width, NULL);
	width /= PANGO_SCALE;
	plot->left = width + TEXT_GAP_X;
	if (plot->flags & PLOT_LOGSCALE_X)
		sprintf(buf, "%.*f", MAX(-(int)plot->xmax, 0), pow(10, plot->xmax));
	else
		sprintf(buf, "%.*f", get_precision(plot->xmax), plot->xmax);
	pango_layout_set_text(layout, buf, -1);
	pango_layout_get_size(layout, &width, &height);
	width /= PANGO_SCALE;  height /= PANGO_SCALE;
	plot->bottom = height + TEXT_GAP_Y;
	plot->top = height/2;
	plot->right = width/2;
	g_object_unref(layout);
}

void gtk_plot_set_xrange(GtkPlot *plot, double xmin, double xmax) {
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));
	g_return_if_fail(xmax > xmin);

	plot->flags &= ~PLOT_AUTO_X;
	plot->xmin = xmin;
	plot->xmax = xmax;

	if (!plot->frozen) gtk_plot_update(plot);
}

void gtk_plot_set_yrange(GtkPlot *plot, double ymin, double ymax) {
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));
	g_return_if_fail(ymax > ymin);

	plot->flags &= ~PLOT_AUTO_X;
	plot->ymin = ymin;
	plot->ymax = ymax;

	if (!plot->frozen) gtk_plot_update(plot);
}

void gtk_plot_set_flags(GtkPlot *plot, GtkPlotFlags flags) {
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));

	if (flags == plot->flags) return;
	plot->flags = flags;
	if (!plot->frozen) gtk_plot_update(plot);
}

void gtk_plot_start_new_line(GtkPlot *plot) {
	GtkPlotLine line;
	
	g_return_if_fail(plot);
	g_return_if_fail(GTK_IS_PLOT(plot));

	line.points = g_array_new(FALSE, FALSE, sizeof(GtkPlotPoint));
	g_array_append_val(plot->lines, line);
}

static void gtk_plot_realize(GtkWidget *widget) {
	GtkPlot *plot;
	GdkWindowAttr attributes;
	gint attributes_mask;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_PLOT(widget));
	
	plot = GTK_PLOT(widget);
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
	gdk_window_set_user_data(widget->window, plot);
	
	widget->style = gtk_style_attach(widget->style, widget->window);
	gtk_style_set_background(widget->style, widget->window, GTK_STATE_ACTIVE);
}

static void gtk_plot_size_allocate(GtkWidget *widget, GtkAllocation *allocation) {
	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_PLOT(widget));
	g_return_if_fail(allocation != NULL);

	widget->allocation = *allocation;
	
	if (GTK_WIDGET_REALIZED(widget))
		gdk_window_move_resize(widget->window,
			allocation->x, allocation->y, allocation->width, allocation->height);
}

#define X(_x) (MARGIN + plot->left + ((_x)-plot->xmin)*xscale - ev->area.x)
#define Y(_y) (widget->allocation.height - (MARGIN + plot->bottom + ((_y)-plot->ymin)*yscale) - ev->area.y)

static gint gtk_plot_expose(GtkWidget *widget, GdkEventExpose *ev) {
	GtkPlot *plot;
	GdkGC *gc;
	GdkPixmap *drawbuffer;
	int i, j;
	static GdkColor black = {0};
	static GdkColor white = {0};
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
	static int ncolors = sizeof(color)/sizeof(color[0]);
	double xscale, yscale, tic;
	GtkPlotPoint lastp;
	PangoLayout *layout;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(GTK_IS_PLOT(widget), FALSE);
	g_return_val_if_fail(ev != NULL, FALSE);
	g_return_val_if_fail(GTK_WIDGET_DRAWABLE(widget), FALSE);

	plot = GTK_PLOT(widget);
	g_return_val_if_fail(!plot->frozen, FALSE);

	if (white.pixel == 0) {
		GdkColormap *cmap = gdk_window_get_colormap(widget->window);
		gdk_color_black(cmap, &black);
		gdk_color_white(cmap, &white);
		for (i=0; i<ncolors; i++)
			gdk_color_alloc(cmap, &color[i]);
	}

	if (plot->lines->len == 0) {
		gc = gdk_gc_new(widget->window);
		gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->bg[GTK_STATE_NORMAL]);
		gdk_draw_rectangle(widget->window, gc, TRUE, ev->area.x, ev->area.y,
			ev->area.width, ev->area.height);
		gdk_gc_destroy(gc);
		return TRUE;
	}

	g_return_val_if_fail(plot->xtic > 0, FALSE);
	g_return_val_if_fail(plot->ytic > 0, FALSE);
	g_return_val_if_fail(plot->xmax > plot->xmin, FALSE);
	g_return_val_if_fail(plot->ymax > plot->ymin, FALSE);

	xscale = (widget->allocation.width - plot->left - plot->right - 2*MARGIN) /
		(plot->xmax - plot->xmin);
	yscale = (widget->allocation.height - plot->bottom - plot->top - 2*MARGIN) /
		(plot->ymax - plot->ymin);

	drawbuffer = gdk_pixmap_new(widget->window,
		ev->area.width, ev->area.height, -1);
	gc = gdk_gc_new(drawbuffer);
	gdk_gc_set_foreground(gc, &gtk_widget_get_style(widget)->bg[GTK_STATE_NORMAL]);
	gdk_draw_rectangle(drawbuffer, gc, TRUE, 0, 0,
		ev->area.width, ev->area.height);
	gdk_gc_set_foreground(gc, &black);

	/* draw axes */
	gdk_draw_line(drawbuffer, gc,
		X(plot->xmin), Y(plot->ymin), X(plot->xmax), Y(plot->ymin));
	gdk_draw_line(drawbuffer, gc,
		X(plot->xmin), Y(plot->ymin), X(plot->xmin), Y(plot->ymax));

	/* draw tics */
	layout = gtk_widget_create_pango_layout(widget, "0");
	for (tic=plot->xmin; tic<=plot->xmax; tic+=plot->xtic) {
		char buf[64];
		int width;

		gdk_draw_line(drawbuffer, gc, X(tic), Y(plot->ymin)-2, X(tic), Y(plot->ymin)+2);
		if (plot->flags & PLOT_LOGSCALE_X)
			sprintf(buf, "%.*f", MAX(-(int)tic, 0), pow(10, tic));
		else
			sprintf(buf, "%.*f", get_precision(tic), tic);
		pango_layout_set_text(layout, buf, -1);
		pango_layout_get_size(layout, &width, NULL);
		width /= PANGO_SCALE;
		gdk_draw_layout(drawbuffer, gc, X(tic)-width/2, Y(plot->ymin)+TEXT_GAP_Y, layout);
	}
	for (tic=plot->ymin; tic<=plot->ymax; tic+=plot->ytic) {
		char buf[64];
		int width, height;

		gdk_draw_line(drawbuffer, gc,
			X(plot->xmin)-2, Y(tic), X(plot->xmin)+2, Y(tic));
		if (plot->flags & PLOT_LOGSCALE_Y)
			sprintf(buf, "%.*f", MAX(-(int)tic, 0), pow(10, tic));
		else
			sprintf(buf, "%.*f", get_precision(tic), tic);
		pango_layout_set_text(layout, buf, -1);
		pango_layout_get_size(layout, &width, &height);
		width /= PANGO_SCALE;  height /= PANGO_SCALE;
		gdk_draw_layout(drawbuffer, gc,
			X(plot->xmin)-width-TEXT_GAP_Y, Y(tic)-height/2, layout);
	}
	g_object_unref(layout);

	/* draw points */
	for (i=0; i<plot->lines->len; i++) {
		gdk_gc_set_foreground(gc, &color[i % ncolors]);
		for (j=0; j<LINE(plot, i).points->len; j++) {
			GtkPlotPoint p = POINT(plot, i, j);
			int x, y;

			if (plot->flags & PLOT_LOGSCALE_X) p.x = log10(p.x);
			if (plot->flags & PLOT_LOGSCALE_Y) p.y = log10(p.y);
			x = X(p.x);  y = Y(p.y);

			if (j > 0 && (plot->flags & PLOT_LINES))
				gdk_draw_line(drawbuffer, gc, X(lastp.x), Y(lastp.y), x, y);
			if (plot->flags & PLOT_POINTS)
				gdk_draw_rectangle(drawbuffer, gc, FALSE, x-2, y-2, 4, 4);
			if (i == plot->highlight_line && j == plot->highlight_point) {
				gdk_gc_set_foreground(gc, &black);
				gdk_draw_arc(drawbuffer, gc, FALSE, x-5, y-5, 10, 10, 0, 64*360);
				gdk_gc_set_foreground(gc, &color[i % ncolors]);
			}

			lastp = p;
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

static void gtk_plot_set_highlight(GtkPlot *plot, int click_x, int click_y) {
	int distsq = 1<<20, closest_line = -1, closest_point = -1;
	int i, j;
	double xscale, yscale;
	GtkWidget *widget = GTK_WIDGET(plot);
	struct { struct { int x, y; } area; } fakeev = { { 0, 0 } }, *ev = &fakeev;

	xscale = (widget->allocation.width - plot->left - plot->right - 2*MARGIN) /
		(plot->xmax - plot->xmin);
	yscale = (widget->allocation.height - plot->bottom - plot->top - 2*MARGIN) /
		(plot->ymax - plot->ymin);

	for (i=0; i<plot->lines->len; i++) {
		for (j=0; j<LINE(plot, i).points->len; j++) {
			GtkPlotPoint p = POINT(plot, i, j);
			int x, y, this_distsq;

			if (plot->flags & PLOT_LOGSCALE_X) p.x = log10(p.x);
			if (plot->flags & PLOT_LOGSCALE_Y) p.y = log10(p.y);
			x = X(p.x);  y = Y(p.y);

			this_distsq = (x-click_x) * (x-click_x) + (y-click_y) * (y-click_y);
			if (this_distsq < distsq) {
				closest_line = i;
				closest_point = j;
				distsq = this_distsq;
			}
		}
	}

	if (distsq < 4000) {
		plot->highlight_line = closest_line;
		plot->highlight_point = closest_point;
	}
	else
		plot->highlight_line = plot->highlight_line = -1;
}

static gint gtk_plot_button_press(GtkWidget *widget, GdkEventButton *ev) {
	GtkPlot *plot;
	GtkPlotPoint *point;

	g_return_val_if_fail(widget, FALSE);
	g_return_val_if_fail(GTK_IS_PLOT(widget), FALSE);
	g_return_val_if_fail(ev, FALSE);
	plot = GTK_PLOT(widget);
	gtk_plot_set_highlight(plot, (int)ev->x, (int)ev->y);

	if (plot->highlight_line == -1)
		point = NULL;
	else
		point = &POINT(plot, plot->highlight_line, plot->highlight_point);
	g_signal_emit(G_OBJECT(plot), plot_signals[POINT_CLICKED], 0, point);

	gtk_widget_queue_draw(widget);

	return TRUE;
}

static int get_precision(double n) {
	if (n == 0.0 || n == floor(n)) return 0;
	return MAX(0, -(int)floor(log10(n-floor(n))));
}
