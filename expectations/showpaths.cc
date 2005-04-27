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
static MYSQL mysql;
static int malformed_paths_count = 0;

int main(int argc, char **argv) {
	std::map<std::string,Recognizer*>::const_iterator rp;
	int just_one = -1;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage:\n  %s table-name [pathid]\n\n", argv[0]);
		return 1;
	}

	if (argc == 3) just_one = atoi(argv[2]);

	const char *base = argv[1];

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	MYSQL_RES *res;
	MYSQL_ROW row;

	std::set<int> pathids;
	if (just_one == -1)
		get_path_ids(&mysql, base, &pathids);

	fprintf(stderr, "Reading threads...");
	run_sqlf(&mysql, "SELECT * from %s_threads", base);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		threads[atoi(row[0])] = new PathThread(row);
	}
	mysql_free_result(res);
	fprintf(stderr, " done: %d found.\n", threads.size());

	if (just_one != -1)
		read_path(base, just_one);
	else
		for (std::set<int>::const_iterator p=pathids.begin(); p!=pathids.end(); p++)
			read_path(base, *p);

	printf("malformed paths: %d\n", malformed_paths_count);

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

	printf("====================[ path %d ]====================\n", pathid);
	path->print();
}
