/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include "events.h"

bool header_only = false;

static void usage(const char *prog) {
	fprintf(stderr, "Usage:\n  %s [-H] file [file [file [...]]]\n\n", prog);
	fprintf(stderr, "  -H = show headers only\n");
	exit(1);
}

int main(int argc, char **argv) {
	int i;
	char c;
	while ((c = getopt(argc, argv, "H")) != -1) {
		switch (c) {
			case 'H':  header_only = true; break;
			default:   usage(argv[0]);
		}
	}

	if (argc-optind < 1)
		usage(argv[0]);

	if (!header_only) printf("<trace>\n");
	for (i=optind; i<argc; i++) {
		int version = -1;
		FILE *fp = !strcmp(argv[i], "-") ? stdin : fopen(argv[i], "r");
		if (!fp) { perror(argv[i]); continue; }
		if (!header_only) printf("  <log name=\"%s\">\n", argv[i]); 
		Event *e = read_event(version, fp);
		while (e) {
			if (e->type() == EV_HEADER) {
				Header *hdr = (Header*)e;
				version = hdr->version;
				if (header_only) {
					const char *slash;
					if (!strcmp(argv[i], "-"))
						slash = "<stdin>";
					else {
						slash = strrchr(argv[i], '/');
						slash = slash ? slash+1 : argv[i];
					}
					printf("%s:  %-12s %s   pid=%d tid=%d ppid=%d uid=%d   %s",
						slash, hdr->processname, hdr->hostname, hdr->pid, hdr->tid, hdr->ppid, hdr->uid, ctime(&hdr->tv.tv_sec));
					delete e;
					break;
				}
			}
			else if (!header_only)
				e->print(stdout, 2);
			delete e;
			e = read_event(version, fp);
		}
		if (!header_only) printf("  </log>\n");
		fclose(fp);
	}
	if (!header_only) printf("</trace>\n");
	return 0;
}
