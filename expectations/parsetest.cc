/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include "path.h"
#include "aggregates.h"
#include "exptree.h"
#include "expect.tab.hh"

static void usage(const char *prog);

int main(int argc, char **argv) {
	std::vector<RecognizerBase*>::const_iterator rp;
	unsigned int i;

	if (argc != 2) usage(argv[0]);

	if (!expect_parse(argv[1])) return 1;
	printf("%zd recognizers registered.\n", recognizers.size());
	for (rp=recognizers.begin(); rp!=recognizers.end(); rp++)
		(*rp)->print();
	printf("----------------------------------------------------------------\n");
	printf("%zd aggregates registered.\n", aggregates.size());
	for (i=0; i<aggregates.size(); i++) {
		aggregates[i]->print();
		putchar('\n');
	}
	printf("----------------------------------------------------------------\n");
}

static void usage(const char *prog) {
	fprintf(stderr, "Usage:\n  %s expect-file\n\n", prog);
	exit(1);
}
