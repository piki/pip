#include <getopt.h>
#include <fcntl.h>
#include <stdio.h>
#include <string>
#include "client.h"

static void usage(const char *prog);
static void read_file(const char *fn);

int main(int argc, char **argv) {
	char c;
	while ((c = getopt(argc, argv, "u")) != -1) {
		switch (c) {
			case 'u':  save_unmatched_sends = true;  break;
			default:   usage(argv[0]);
		}
	}
	if (argc - optind < 2) usage(argv[0]);

	reconcile_init(argv[optind]);

	for (int i=optind+1; i<argc; i++)
		read_file(argv[i]);

	reconcile_done();

	printf("There were %d error%s\n", errors, errors==1?"":"s");
	return errors > 0;
}

static void read_file(const char *fn) {
	fprintf(stderr, "Reading %s\n", fn);
	FILE *fp;
	int is_pipe = 0;
	if (!strcmp(fn+strlen(fn)-4, ".bz2")) {
		std::string cmd("bunzip2 -c ");
		cmd.append(fn);
		fp = popen(cmd.c_str(), "r");
		is_pipe = 1;
	}
	else if (!strcmp(fn+strlen(fn)-3, ".gz")) {
		std::string cmd("gunzip -c ");
		cmd.append(fn);
		fp = popen(cmd.c_str(), "r");
		is_pipe = 1;
	}
	else
		fp = fopen(fn, "r");
	if (!fp) { perror(fn); return; }

	Client cl;

	int n;
	char buf[65536];
	while ((n = fread(buf, 1, sizeof(buf), fp)) != 0) {
		if (n == -1) {
			perror("fread");
			exit(1);
		}
		cl.append(buf, n);
	}

	if (is_pipe) pclose(fp); else fclose(fp);
	cl.end();
}

static void usage(const char *prog) {
	fprintf(stderr, "Usage:  %s [-u] table-name trace-file [trace-file [...]]\n\n", prog);
	fprintf(stderr, "  -u    Add unreceived sends to the database.\n\n");
	fprintf(stderr, "        The default behavior is to ignore them.\n\n");
	exit(1);
}
