/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <piki/mainloop.h>
#include <piki/socklib.h>
#include "client.h"
#include "reconcile.h"

static void on_new_connection(int fd, IOCondition cond, void *data);
static void on_readable(int fd, IOCondition cond, void *data);
static void on_sigint(int sig) { mainloop_quit(); }

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:\n  %s table-name port\n\n", argv[0]);
		return 1;
	}
	reconcile_init(argv[1]);
	int port = atoi(argv[2]);
	int lfd = sock_listen(port);
	mainloop_add_input(lfd, IO_READ, on_new_connection, NULL);

	signal(SIGINT, on_sigint);
	mainloop_run();

	reconcile_done();

	printf("There were %d error%s\n", errors, errors==1?"":"s");
	return errors > 0;
}

static int nclients = 0;
static void on_new_connection(int fd, IOCondition cond, void *data) {
	assert(cond == IO_READ);

	struct sockaddr_in sock;
	socklen_t socklen = sizeof(sock);
	int afd = accept(fd, (struct sockaddr*)&sock, &socklen);

	printf("New connection from %s:%d on fd=%d\n",
		inet_ntoa(sock.sin_addr), ntohs(sock.sin_port), afd);

	++nclients;
	Client *cl = new Client;
	int handle = mainloop_add_input(afd, IO_READ, on_readable, cl);
	cl->handle = handle;
}

static void on_readable(int fd, IOCondition cond, void *data) {
	assert(cond == IO_READ);
	Client *cl = (Client*)data;
	assert(cl);

	char buf[4096];
	int n = read(fd, buf, sizeof(buf));
	if (n == -1) { perror("read"); exit(1); }
	if (n == 0) {   // eof
		close(fd);
		mainloop_remove_input(cl->handle);
		cl->end();
		delete cl;
		if (--nclients == 0) mainloop_quit();
		return;
	}
	cl->append(buf, n);
}
