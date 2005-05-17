#ifndef __GTK_PLOT_H__
#define __GTK_PLOT_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTK_PLOT(obj)          GTK_CHECK_CAST(obj, gtk_plot_get_type(), GtkPlot)
#define GTK_PLOT_CLASS(klass)  GTK_CHECK_CLASS_CAST(klass, gtk_plot_get_type(), GtkPlotClass)
#define GTK_IS_PLOT(obj)       GTK_CHECK_TYPE(obj, gtk_plot_get_type())

typedef struct _GtkPlot       GtkPlot;
typedef struct _GtkPlotClass  GtkPlotClass;
typedef struct _GtkPlotPoint  GtkPlotPoint;
typedef enum {
	PLOT_AUTO_X = 1<<0,
	PLOT_AUTO_Y = 1<<1,
	PLOT_LOGSCALE_X = 1<<2,
	PLOT_LOGSCALE_Y = 1<<3,
	PLOT_LINES = 1<<4,
	PLOT_POINTS = 1<<5,
	PLOT_X0 = 1<<6,
	PLOT_Y0 = 1<<7,
	PLOT_DEFAULTS = PLOT_AUTO_X | PLOT_AUTO_Y | PLOT_LINES | PLOT_POINTS
} GtkPlotFlags;

struct _GtkPlot {
	GtkWidget widget;
	GArray *lines;
	gboolean frozen;
	double xmin, xmax, ymin, ymax, xtic, ytic;
	int left, bottom, right, top;  /* margins */
	GtkPlotFlags flags;
	int highlight_line, highlight_point;
};

struct _GtkPlotClass {
	GtkWidgetClass parent_class;

	void (* point_clicked) (GtkPlot *plot, GtkPlotPoint *p);
};

struct _GtkPlotPoint {
	double x, y;
};

GType      gtk_plot_get_type(void);
GtkWidget* gtk_plot_new(GtkPlotFlags flags);
void       gtk_plot_free(GtkPlot *plot);
void       gtk_plot_clear(GtkPlot *plot);
void       gtk_plot_start_new_line(GtkPlot *plot);
void       gtk_plot_add_point(GtkPlot *plot, double x, double y);
void       gtk_plot_freeze(GtkPlot *plot);
void       gtk_plot_thaw(GtkPlot *plot);
void       gtk_plot_set_xrange(GtkPlot *plot, double xmin, double xmax);
void       gtk_plot_set_yrange(GtkPlot *plot, double ymin, double ymax);
void       gtk_plot_set_flags(GtkPlot *plot, GtkPlotFlags flags);

#ifdef __cplusplus
}
#endif

#endif
