/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef __GTK_PATHTL_H__
#define __GTK_PATHTL_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>
#include "path.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GTK_PATHTL(obj)          GTK_CHECK_CAST(obj, gtk_pathtl_get_type(), GtkPathTL)
#define GTK_PATHTL_CLASS(klass)  GTK_CHECK_CLASS_CAST(klass, gtk_pathtl_get_type(), GtkPathTLClass)
#define GTK_IS_PATHTL(obj)       GTK_CHECK_TYPE(obj, gtk_pathtl_get_type())

typedef struct _GtkPathTL       GtkPathTL;
typedef struct _GtkPathTLClass  GtkPathTLClass;
typedef struct _GtkPathTLNode   GtkPathTLNode;
typedef struct _GtkPathTLEdge   GtkPathTLEdge;

typedef enum {
	PATHTL_SHOW_SUBTASKS=1,
	PATHTL_SHOW_NOTICES=2,
	PATHTL_SHOW_MESSAGES=4,
	PATHTL_DEFAULTS = PATHTL_SHOW_SUBTASKS|PATHTL_SHOW_NOTICES|PATHTL_SHOW_MESSAGES
} GtkPathtlFlags;

struct _GtkPathTL {
	GtkWidget widget;
	const Path *path;
	int maxx, height;
	double zoom;
	struct timeval trace_start;
	GtkPathtlFlags flags;
	const PathEvent *where_clicked;
};

struct _GtkPathTLClass {
	GtkWidgetClass parent_class;

	void (* node_clicked) (GtkPathTL *pathtl, PathEvent *node);
	void (* node_activated) (GtkPathTL *pathtl, PathEvent *node);
	void (* zoom_changed) (GtkPathTL *pathtl, double zoom);
};

GType         gtk_pathtl_get_type();
GtkWidget*    gtk_pathtl_new(const struct timeval &trace_start);
void          gtk_pathtl_free(GtkPathTL *pathtl);
void          gtk_pathtl_set(GtkPathTL *pathtl, const Path *path);
void          gtk_pathtl_set_zoom(GtkPathTL *pathtl, double zoom);
void          gtk_pathtl_set_trace_start(GtkPathTL *pathtl, const struct timeval &trace_start);
void          gtk_pathtl_set_flags(GtkPathTL *pathtl, GtkPathtlFlags flags);

#ifdef __cplusplus
}
#endif

#endif
