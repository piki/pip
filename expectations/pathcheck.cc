#include <string>
#include <stdarg.h>
#include <stdio.h>
#include "path.h"
#include <mysql/mysql.h>
#include <map>
#include "exptree.h"

inline static timeval tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}
static void run_sql(const char *fmt, ...)
	__attribute__((__format__(printf,1,2)));
struct ltstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1, s2) < 0;
  }
};
static MYSQL mysql;

extern bool expect_parse(const char *filename);
extern std::vector<Recognizer*> recognizers;
int main(int argc, char **argv) {
	unsigned int i;

	if (argc != 3) {
		fprintf(stderr, "Usage:\n  %s table-name expect-file\n\n", argv[0]);
		return 1;
	}

	if (!expect_parse(argv[2])) return 1;
	printf("%d recognizers registered:\n", recognizers.size());
	for (i=0; i<recognizers.size(); i++)
		recognizers[i]->print();
	printf("------\n");
	
	std::string base(argv[1]);
	std::string table_tasks = base + "_tasks";
	std::string table_notices = base + "_notices";

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	/* !! "order by" is expensive -- better to allow inserting children first */
	std::map<int, Path> path;
	run_sql("SELECT * FROM %s order by start", table_tasks.c_str());
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		int path_id = atoi(row[0]);
		PathTask *pt = new PathTask(row);
		path[path_id].insert(pt);
	}
	mysql_free_result(res);

	run_sql("SELECT * FROM %s", table_notices.c_str());
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		int path_id = atoi(row[0]);
		PathNotice *pn = new PathNotice(row);
		path[path_id].insert(pn);
	}
	mysql_free_result(res);

	int match_count[recognizers.size()+1];
	bzero(match_count, sizeof(int)*(recognizers.size()+1));
	printf("%d paths to check\n", path.size());
	for (std::map<int, Path>::iterator p=path.begin(); p!=path.end(); p++) {
		int count = 0;
		p->second.done_inserting();
//		p->second.print();
		bool printed = false;
		for (i=0; i<recognizers.size(); i++) {
			bool resources = true;
			if (recognizers[i]->check(p->second, &resources)) {
				count++;
				if (!resources) {
					if (!printed) {
						printed = true;
						printf("%d:\n", p->first);
						p->second.print();
					}
					printf("  %d (%s) matched, resources false\n",
						i, recognizers[i]->name->name.c_str());
				}
			}
		}
		if (!count)
			printf("  nothing matched\n");
		match_count[count]++;
	}

	for (i=0; i<=recognizers.size(); i++)
		printf("paths matching %d recognizer(s): %d\n", i, match_count[i]);

	mysql_close(&mysql);
	return 0;
}

static void run_sql(const char *fmt, ...) {
	char query[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(query, fmt, arg);
	va_end(arg);
	//puts(query);
	if (mysql_query(&mysql, query) != 0) {
		fprintf(stderr, "Database error:\n");
		fprintf(stderr, "  QUERY: \"%s\"\n", query);
		fprintf(stderr, "  MySQL error: \"%s\"\n", mysql_error(&mysql));
		exit(1);
	}
}
