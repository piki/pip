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

static void read_path(const char *base, int pathid);
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
static MYSQL mysql;
static int malformed_paths_count = 0;

static std::map<Path*, int, ltPathShape> shapes;        // shape the same
static std::map<Path*, int, ltPathExact> shapes_names;  // shape and names the same (exact)

int main(int argc, char **argv) {
	std::map<std::string,Recognizer*>::const_iterator rp;

	if (argc != 2) {
		fprintf(stderr, "Usage:\n  %s table-name\n\n", argv[0]);
		return 1;
	}

	const char *base = argv[1];

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	MYSQL_RES *res;
	MYSQL_ROW row;

	std::set<int> pathids;
	get_path_ids(&mysql, base, &pathids);

	fprintf(stderr, "Reading threads...");
	run_sqlf(&mysql, "SELECT * from %s_threads", base);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		threads[atoi(row[0])] = new PathThread(row);
	}
	mysql_free_result(res);
	fprintf(stderr, " done: %d found.\n", (int)threads.size());

	for (std::set<int>::const_iterator p=pathids.begin(); p!=pathids.end(); p++) {
		read_path(base, *p);
	}

	printf("malformed paths: %d\n", malformed_paths_count);

	printf("%d unique shapes:\n", (int)shapes.size());
	for (std::map<Path*,int>::const_iterator p=shapes.begin(); p!=shapes.end(); p++) {
		printf("====[ %d time(s) ]====\n", p->second);
		p->first->print();
	}

	printf("%d unique shapes+names:\n", (int)shapes_names.size());
	for (std::map<Path*,int>::const_iterator p=shapes_names.begin(); p!=shapes_names.end(); p++) {
		printf("====[ %d time(s) ]====\n", p->second);
		p->first->print();
	}

	mysql_close(&mysql);
	return 0;
}

static void read_path(const char *base, int pathid) {
	Path *path = new Path(&mysql, base, pathid);
	if (!path->valid()) {
		printf("# path %d malformed\n", pathid);
		malformed_paths_count++;
		delete path;
	}

	shapes[path]++;
	shapes_names[path]++;
}
