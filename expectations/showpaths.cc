/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include "path.h"
#include <map>
#include <set>
#include "aggregates.h"
#include "exptree.h"
#include "rcfile.h"
#include "pathfactory.h"

static void read_path(PathFactory *pf, int pathid);
static int malformed_paths_count = 0;

int main(int argc, char **argv) {
	std::map<std::string,RecognizerBase*>::const_iterator rp;
	int just_one = -1;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage:\n  %s table-name [pathid]\n\n", argv[0]);
		return 1;
	}

	if (argc == 3) just_one = atoi(argv[2]);

	const char *base = argv[1];

	PathFactory *pf = path_factory(base);
	if (!pf->valid()) return 1;

	if (just_one == -1) {
		std::vector<int> pathids = pf->get_path_ids();
		for (std::vector<int>::const_iterator p=pathids.begin(); p!=pathids.end(); p++)
			read_path(pf, *p);
	}
	else
		read_path(pf, just_one);

	printf("malformed paths: %d\n", malformed_paths_count);

	delete pf;

	return 0;
}

static void read_path(PathFactory *pf, int pathid) {
	Path *path = pf->get_path(pathid);
	if (!path->valid()) {
		printf("# path %d malformed\n", pathid);
		malformed_paths_count++;
	}

	printf("====================[ path %d ]====================\n", pathid);
	path->print();
	delete path;
}
