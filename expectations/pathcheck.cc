#include <assert.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include "path.h"
#include <mysql/mysql.h>
#include <map>
#include <set>
#include "aggregates.h"
#include "exptree.h"

static bool check_path(const char *base, int pathid);
inline static timeval tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}
struct ltstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1, s2) < 0;
  }
};
static MYSQL mysql;
static std::vector<int> match_tally;     // how many paths matched N recognizers
static std::vector<int> match_count;     // how many paths matched recognizer N
static std::vector<int> resources_count; // # paths matching N, but over limits
static int malformed_paths_count = 0;

int main(int argc, char **argv) {
	std::map<std::string,Recognizer*>::const_iterator rp;
	unsigned int i;
	int just_one = -1;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "Usage:\n  %s table-name expect-file [pathid]\n\n", argv[0]);
		return 1;
	}

	if (argc == 4) just_one = atoi(argv[3]);

	if (!expect_parse(argv[2])) return 1;
	printf("%d recognizers registered.\n", recognizers.size());
#if 0
	for (rp=recognizers.begin(); rp!=recognizers.end(); rp++)
		rp->second->print();
	printf("----------------------------------------------------------------\n");
#endif
	printf("%d aggregates registered.\n", aggregates.size());
#if 0
	for (i=0; i<aggregates.size(); i++)
		aggregates[i]->print_tree();
	printf("----------------------------------------------------------------\n");
#endif
	match_tally.insert(match_tally.end(), recognizers.size()+1, 0);
	match_count.insert(match_count.end(), recognizers.size(), 0);
	resources_count.insert(resources_count.end(), recognizers.size(), 0);

	const char *base = argv[1];

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	std::set<int> pathids;
	if (just_one == -1)
		get_path_ids(&mysql, base, &pathids);
	get_threads(&mysql, base, &threads);

	if (just_one != -1)
		check_path(base, just_one);
	else
		for (std::set<int>::const_iterator p=pathids.begin(); p!=pathids.end(); p++)
			check_path(base, *p);

	printf("malformed paths: %d\n", malformed_paths_count);
	for (i=0; i<=recognizers.size(); i++)
		printf("paths matching %d validator(s): %d\n", i, match_tally[i]);
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); rp++,i++) {
		printf("paths matching %s \"%s\": %d",
			rp->second->validating ? "validator" : "recognizer",
			rp->second->name.c_str(), match_count[i]);
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

	mysql_close(&mysql);
	return 0;
}

static bool check_path(const char *base, int pathid) {
	std::map<std::string,Recognizer*>::const_iterator rp;
	unsigned int i;
	Path path(&mysql, base, pathid);

	if (!path.valid()) {
		printf("# path %d malformed -- not checked\n", pathid);
		malformed_paths_count++;
		return true;
	}
	int tally = 0;
	bool printed = false;
#if 0
	printf("\n# path %d\n", pathid);
	path.print();
	putchar('\n');
	printed = true;
#endif
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++) {
		bool resources = true;
		if (rp->second->check(&path, &resources)) {
			printf("%d ---> matched %s\n", pathid, rp->second->name.c_str());
			match_count[i]++;
			if (rp->second->validating) tally++;
			if (!resources) {
				resources_count[i]++;
				if (!printed) {
					printed = true;
					printf("# path %d:\n", pathid);
					path.print();
				}
				printf("  %d (%s) matched, resources false\n",
					i, rp->second->name.c_str());
			}
		}
	}
	if (!tally) {
		if (!printed) {
			printf("# path %d:\n", pathid);
			path.print();
		}
		printf("%d ---> nothing matched\n", pathid);
	}
	match_tally[tally]++;
	return tally > 0;
}
