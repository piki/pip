/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <stdio.h>
#include <gtk/gtk.h>
#include "plot.h"

static void point_clicked(GtkPlot *plot, GtkPlotPoint *point);

int main(int argc, char **argv) {
	GtkWidget *w, *plot;
	int i;
	char *string = "ABCDEFGHIJKLMNOPQRSTUV";

	gtk_init(&argc, &argv);

	w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(w), 640, 480);
	plot = gtk_plot_new(PLOT_DEFAULTS | PLOT_KEY_UR);
	gtk_container_add(GTK_CONTAINER(w), plot);
	gtk_widget_show_all(w);
	g_signal_connect(GTK_OBJECT(w), "delete_event", gtk_main_quit, NULL);
	g_signal_connect(GTK_OBJECT(plot), "point_clicked", GTK_SIGNAL_FUNC(point_clicked), NULL);

	gtk_plot_freeze(GTK_PLOT(plot));
	gtk_plot_start_new_line(GTK_PLOT(plot), "first");
	for (i=1; i<10; i++)
		gtk_plot_add_point(GTK_PLOT(plot), i, i*i, string+i-1);
	gtk_plot_start_new_line(GTK_PLOT(plot), "second");
	for (i=1; i<10; i++)
		gtk_plot_add_point(GTK_PLOT(plot), i, (i-3)*(i-3), string+i-1+10);
	gtk_plot_thaw(GTK_PLOT(plot));

	gtk_main();

	return 0;
}

static void point_clicked(GtkPlot *plot, GtkPlotPoint *point) {
	if (point)
		printf("point clicked: %.3f %.3f \"%s\"\n", point->x, point->y, (char*)point->user_data);
	else
		printf("point cleared\n");
}
