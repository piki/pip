#include <stdio.h>
#include <string.h>
#include "events.h"

int main(int argc, char **argv) {
	int i;
	if (argc < 2) {
		fprintf(stderr, "Usage:\n  %s file [file [file [...]]]\n", argv[0]);
		return 1;
	}

	printf("<trace>\n");
	for (i=1; i<argc; i++) {
		int version = -1;
		FILE *fp = fopen(argv[i], "r");
		if (!fp) { perror(argv[i]); continue; }
		printf("  <log name=\"%s\">\n", argv[i]); 
		Event *e = read_event(version, fp);
		while (e) {
			e->print(stdout, 2);
			if (e->type() == EV_HEADER)
				version = ((Header*)e)->version;
			delete e;
			e = read_event(version, fp);
		}
		printf("  </log>\n");
		fclose(fp);
	}
	printf("</trace>\n");
	return 0;
}
