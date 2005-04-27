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

struct _GtkPathTL {
	GtkWidget widget;
	const Path *path;
	gboolean times_to_scale;
};

struct _GtkPathTLClass {
	GtkWidgetClass parent_class;

	void (* node_clicked) (GtkPathTL *pathtl, void *node);
};

GType         gtk_pathtl_get_type();
GtkWidget*    gtk_pathtl_new(void);
void          gtk_pathtl_free(GtkPathTL *pathtl);
void          gtk_pathtl_set(GtkPathTL *pathtl, const Path *path);
void          gtk_pathtl_set_times_to_scale(GtkPathTL *pathtl, gboolean scale);

#ifdef __cplusplus
}
#endif

#endif
