#include <fcntl.h>
#include <stdio.h>
#include "client.h"

static void read_file(const char *fn);

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage:\n  %s table-name trace-file [trace-file [...]]\n\n", argv[0]);
		return 1;
	}

	reconcile_init(argv[1]);

	for (int i=2; i<argc; i++)
		read_file(argv[i]);

	reconcile_done();

	printf("There were %d error%s\n", errors, errors==1?"":"s");
	return errors > 0;
}

static void read_file(const char *fn) {
	fprintf(stderr, "Reading %s\n", fn);
	int fd = open(fn, O_RDONLY);
	if (fd == -1) { perror(fn); return; }

	Client cl;

	int n;
	char buf[65536];
	while ((n = read(fd, buf, sizeof(buf))) != 0) {
		if (n == -1) {
			perror("read");
			exit(1);
		}
		cl.append(buf, n);
	}

	close(fd);
	cl.end();
}
