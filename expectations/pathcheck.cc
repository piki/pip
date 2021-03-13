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
#include "expect.tab.hh"
#include "pathfactory.h"
#include "rcfile.h"

static bool check_path(PathFactory *pf, int pathid);
inline static timeval tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}
static std::vector<int> match_tally;     // how many paths matched N recognizers
static std::vector<int> match_count;     // how many paths matched recognizer N
static std::vector<int> resources_count; // # paths matching N, but over limits
static int malformed_paths_count = 0;
static size_t _ign;

static void usage(const char *prog);

static bool verbose = false;
int main(int argc, char **argv) {
	std::vector<RecognizerBase*>::const_iterator rp;
	unsigned int i;
	int just_one = -1;
	char c;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
			case 'v':   verbose = true;  break;
			default: usage(argv[0]);
		}
	}

	if (argc-optind < 2) usage(argv[0]);

	if (argc-optind == 3) just_one = atoi(argv[optind+2]);

	if (!expect_parse(argv[optind+1])) return 1;
	printf("%zd recognizers registered.\n", recognizers.size());
#if 0
	for (rp=recognizers.begin(); rp!=recognizers.end(); rp++)
		(*rp)->print();
	printf("----------------------------------------------------------------\n");
#endif
	printf("%zd aggregates registered.\n", aggregates.size());
#if 0
	for (i=0; i<aggregates.size(); i++)
		aggregates[i]->print_tree();
	printf("----------------------------------------------------------------\n");
#endif
	match_tally.insert(match_tally.end(), recognizers.size()+1, 0);
	match_count.insert(match_count.end(), recognizers.size(), 0);
	resources_count.insert(resources_count.end(), recognizers.size(), 0);

	const char *base = argv[optind];

	PathFactory *pf = path_factory(base);
	if (!pf->valid()) return 1;

	if (just_one == -1) {
		std::vector<int> pathids = pf->get_path_ids();
		for (std::vector<int>::const_iterator p=pathids.begin(); p!=pathids.end(); p++)
			check_path(pf, *p);
	}
	else
		check_path(pf, just_one);

	printf("malformed paths: %d\n", malformed_paths_count);
	for (i=0; i<=recognizers.size(); i++)
		printf("paths matching %d validator(s): %d\n", i, match_tally[i]);
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); rp++,i++) {
		printf("paths matching %s \"%s\": %d",
			path_type_to_string((*rp)->pathtype),
			(*rp)->name.c_str(), match_count[i]);
		if (resources_count[i] != 0)
			printf(" (%d missed limits)\n", resources_count[i]);
		else
			putchar('\n');
	}

	for (i=0; i<aggregates.size(); i++) {
		printf("aggregate %d [ ", i);
		aggregates[i]->print();
		printf(" ] = %s\n", aggregates[i]->check() ? "true" : "false");
	}

	delete pf;
	return 0;
}

static bool check_path(PathFactory *pf, int pathid) {
	std::vector<RecognizerBase*>::const_iterator rp;
	unsigned int i;
	Path *path = pf->get_path(pathid);

	if (!path->valid()) {
		printf("# path %d malformed -- not checked\n", pathid);
		malformed_paths_count++;
		delete path;
		return true;
	}
	int tally = 0;
	bool invalidated = false;
	bool printed = false;
#if 0
	printf("\n# path %d\n", pathid);
	path->print();
	putchar('\n');
	printed = true;
#endif
	std::map<std::string, bool> match_map;
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++) {
		bool resources = true;
		if ((*rp)->check(path, &resources, &match_map)) {
			printf("%d ---> matched %s\n", pathid, (*rp)->name.c_str());
			if (resources) {
				match_count[i]++;
				if ((*rp)->pathtype == VALIDATOR) tally++;
				if ((*rp)->pathtype == INVALIDATOR) invalidated = true;
			}
			else if ((*rp)->pathtype != INVALIDATOR) {
				resources_count[i]++;
				if (!printed) {
					printed = true;
					printf("# path %d:\n", pathid);
					path->print();
				}
				printf("  %d (%s) matched, resources false\n",
					i, (*rp)->name.c_str());
			}
		}
	}
	if (!tally) {
		printf("%d ---> nothing matched\n", pathid);
		if (!printed) path->print();
		if (verbose) {
			debug_failed_matches = true;
			for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++) {
				if ((*rp)->type_string()[0] == 's') continue;  // don't debug set recognizers
				bool resources = false;
				if (!(*rp)->check(path, &resources, NULL))
					_ign = fwrite(debug_buffer.data(), 1, debug_buffer.length(), stdout);
			}
			debug_failed_matches = false;
		}
	}
	if (invalidated) {
		printf("%d ---> invalidated\n", pathid);
		if (!printed) {
			printf("# path %d:\n", pathid);
			path->print();
		}
	}
	match_tally[invalidated ? 0 : tally]++;
	delete path;
	return tally > 0;
}

static void usage(const char *prog) {
	fprintf(stderr, "Usage: %s [-v] table-name expect-file [pathid]\n\n", prog);
	fprintf(stderr, "  -v      verbose: show why a path does not match\n\n");
	exit(1);
}
