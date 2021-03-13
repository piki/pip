/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <getopt.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include "path.h"
#include <map>
#include <set>
#include "aggregates.h"
#include "exptree.h"
#include "pathfactory.h"
#include "rcfile.h"

static void read_path(PathFactory *pf, int pathid);
static void usage(const char *prog) {
	fprintf(stderr, "Usage:\n  %s [options] table-name pathid\n\n", prog);
	exit(1);
}

static bool use_regex = false;

int main(int argc, char **argv) {
	std::map<std::string,RecognizerBase*>::const_iterator rp;

	char c;
	while ((c = getopt(argc, argv, "r")) != -1) {
		switch (c) {
			case 'r': use_regex = true;  break;
			default:  usage(argv[0]);
		}
	}
	if (argc - optind != 2) {
		fprintf(stderr, "Usage:\n  %s [options] table-name pathid\n\n", argv[0]);
		return 1;
	}
	const char *base = argv[optind];
	int pathid = atoi(argv[optind+1]);

	PathFactory *pf = path_factory(base);
	if (!pf->valid()) return 1;

	read_path(pf, pathid);

	delete pf;
	return 0;
}

static void read_path(PathFactory *pf, int pathid) {
	Path *path = pf->get_path(pathid);
	if (!path->valid())
		printf("# path %d malformed\n", pathid);

	printf("// source=\"%s\" pathid=%d\n", pf->get_name().c_str(), pathid);
	printf("validator val_%d {\n", pathid);
	path->print_exp();
	printf("}\n");
	delete path;
}
