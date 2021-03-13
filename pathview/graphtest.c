/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include "graph.h"

static void fill_graph(const char *fn, GtkGraph *graph);

int main(int argc, char **argv) {
	GtkWidget *win, *graph;

	gtk_init(&argc, &argv);
	if (argc != 2) {
		fprintf(stderr, "Usage:\n  %s graph.dat\n", argv[0]);
		return 1;
	}

	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	graph = gtk_graph_new();
	gtk_graph_freeze(GTK_GRAPH(graph));
	fill_graph(argv[1], GTK_GRAPH(graph));
	gtk_container_add(GTK_CONTAINER(win), graph);
	gtk_widget_show_all(win);
	gtk_graph_thaw(GTK_GRAPH(graph));

	g_signal_connect(G_OBJECT(win), "delete_event", gtk_main_quit, NULL);
	gtk_main();

	return 0;
}

static void fill_graph(const char *fn, GtkGraph *graph) {
	FILE *fp = fopen(fn, "r");
	if (!fp) {
		perror(fn);
		return;
	}
	GHashTable *hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	char buf[512];
	while (fgets(buf, sizeof(buf), fp)) {
		const char *cmd = strtok(buf, "\t\n");
		const char *first = strtok(NULL, "\t\n");
		const char *second = strtok(NULL, "\t\n");
		if (!strcmp(cmd, "n")) {
			GtkGraphNode *n = gtk_graph_add_node(graph, second);
			g_hash_table_insert(hash, g_strdup(first), n);
		}
		else if (!strcmp(cmd, "l")) {
			GtkGraphNode *n1 = g_hash_table_lookup(hash, first);
			if (!n1) printf("node \"%s\" not found\n", first);
			GtkGraphNode *n2 = g_hash_table_lookup(hash, second);
			if (!n2) printf("node \"%s\" not found\n", second);
			gtk_graph_add_edge(graph, n1, n2, FALSE);
		}
	}
	g_hash_table_destroy(hash);
}
