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
#include "pathfactory.h"
#include "rcfile.h"

static void read_path(PathFactory *pf, int pathid);
struct ltPathShape {
	bool operator()(const Path* p1, const Path* p2) const {
		int res = p1->compare(*p2);
#if 0
		printf("ltPathShape\n");
		p1->print();
		printf("  ~~~~~~~~~~~~~~~~~~\n");
		p2->print();
		printf("  ==> %d\n", res);
		printf("  ==================\n");
#endif
		return res == -PCMP_NONE;
	}
};
struct ltPathExact {
	bool operator()(const Path* p1, const Path* p2) const {
		int res = p1->compare(*p2);
		return res < 0;
	}
};
static int malformed_paths_count = 0;

static std::map<Path*, int, ltPathShape> shapes;        // shape the same
static std::map<Path*, int, ltPathExact> shapes_names;  // shape and names the same (exact)

int main(int argc, char **argv) {
	std::map<std::string,RecognizerBase*>::const_iterator rp;

	if (argc != 2) {
		fprintf(stderr, "Usage:\n  %s table-name\n\n", argv[0]);
		return 1;
	}

	const char *base = argv[1];

	PathFactory *pf = path_factory(base);
	if (!pf->valid()) return 1;

	std::vector<int> pathids = pf->get_path_ids();

	for (std::vector<int>::const_iterator p=pathids.begin(); p!=pathids.end(); p++) {
		read_path(pf, *p);
	}

	printf("malformed paths: %d\n", malformed_paths_count);

	printf("%zd unique shapes:\n", shapes.size());
	for (std::map<Path*,int>::const_iterator p=shapes.begin(); p!=shapes.end(); p++) {
		printf("====[ %d time(s) ]====\n", p->second);
		p->first->print();
	}

	printf("%zd unique shapes+names:\n", shapes_names.size());
	for (std::map<Path*,int>::const_iterator p=shapes_names.begin(); p!=shapes_names.end(); p++) {
		printf("====[ %d time(s) ]====\n", p->second);
		p->first->print();
	}

	delete pf;
	return 0;
}

static void read_path(PathFactory *pf, int pathid) {
	Path *path = pf->get_path(pathid);
	if (!path->valid()) {
		printf("# path %d malformed\n", pathid);
		malformed_paths_count++;
		delete path;
	}

	shapes[path]++;
	shapes_names[path]++;
}
