#include <assert.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include "path.h"
#include <mysql/mysql.h>
#include <map>
#include "aggregates.h"
#include "exptree.h"

static void check_path(const char *base, int pathid);
static void run_sql(const char *fmt, ...)
	__attribute__((__format__(printf,1,2)));
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

int main(int argc, char **argv) {
	std::map<std::string,Recognizer*>::const_iterator rp;
	unsigned int i;

	if (argc != 3) {
		fprintf(stderr, "Usage:\n  %s table-name expect-file\n\n", argv[0]);
		return 1;
	}

	if (!expect_parse(argv[2])) return 1;
	printf("%d recognizers registered:\n", recognizers.size());
	for (rp=recognizers.begin(); rp!=recognizers.end(); rp++)
		rp->second->print();
	printf("----------------------------------------------------------------\n");
	printf("%d aggregates registered:\n", aggregates.size());
	for (i=0; i<aggregates.size(); i++)
		aggregates[i]->print();
	printf("----------------------------------------------------------------\n");
	match_tally.insert(match_tally.end(), recognizers.size()+1, 0);
	match_count.insert(match_count.end(), recognizers.size(), 0);
	resources_count.insert(resources_count.end(), recognizers.size(), 0);

	const char *base = argv[1];

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}

	std::vector<int> pathids;
	fprintf(stderr, "Reading pathids...");
	run_sql("SELECT pathid from %s_paths", base);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		pathids.push_back(atoi(row[0]));
	}
	mysql_free_result(res);
	fprintf(stderr, " done: %d found.\n", pathids.size());

	fprintf(stderr, "Reading threads...");
	run_sql("SELECT * from %s_threads", base);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		threads[atoi(row[0])] = new PathThread(row);
	}
	mysql_free_result(res);
	fprintf(stderr, " done: %d found.\n", threads.size());

	for (i=0; i<pathids.size(); i++)
		check_path(base, pathids[i]);

	for (i=0; i<=recognizers.size(); i++)
		printf("paths matching %d recognizer(s): %d\n", i, match_tally[i]);
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); rp++,i++)
		printf("paths matching recognizer \"%s\": %d (%d over limits)\n",
			rp->second->name->name.c_str(), match_count[i], resources_count[i]);

	for (i=0; i<aggregates.size(); i++)
		printf("aggregate %d: %d\n", i, aggregates[i]->check());

	mysql_close(&mysql);
	return 0;
}

static void run_sql(const char *fmt, ...) {
	char query[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(query, fmt, arg);
	va_end(arg);
	//fprintf(stderr, "SQL(\"%s\")\n", query);
	if (mysql_query(&mysql, query) != 0) {
		fprintf(stderr, "Database error:\n");
		fprintf(stderr, "  QUERY: \"%s\"\n", query);
		fprintf(stderr, "  MySQL error: \"%s\"\n", mysql_error(&mysql));
		exit(1);
	}
}

static void check_path(const char *base, int pathid) {
	Path path;
	std::map<std::string,Recognizer*>::const_iterator rp;
	unsigned int i;

	path.path_id = pathid;

	/* !! "order by" is expensive -- better to allow inserting children first */
	run_sql("SELECT * FROM %s_tasks WHERE pathid=%d ORDER BY start", base, pathid);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathTask *pt = new PathTask(row);
		path.insert(pt);
	}
	mysql_free_result(res);

	run_sql("SELECT * FROM %s_notices WHERE pathid=%d", base, pathid);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathNotice *pn = new PathNotice(row);
		path.insert(pn);
	}
	mysql_free_result(res);

	run_sql("SELECT * FROM %s_messages WHERE pathid=%d", base, pathid);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathMessage *pm = new PathMessage(row);
		path.insert(pm);
	}
	mysql_free_result(res);

	int tally = 0;
	path.done_inserting();
	bool printed = false;
	//printf("# path %d\n", pathid);
	//path.print();
	//printed = true;
	for (i=0,rp=recognizers.begin(); rp!=recognizers.end(); i++,rp++) {
		bool resources = true;
		if (rp->second->check(&path, &resources)) {
			printf("-----> matched %s\n", rp->second->name->name.c_str());
			match_count[i]++;
			tally++;
			if (!resources) {
				resources_count[i]++;
				if (!printed) {
					printed = true;
					printf("%d:\n", pathid);
					path.print();
				}
				printf("  %d (%s) matched, resources false\n",
					i, rp->second->name->name.c_str());
			}
		}
	}
	if (!tally) {
		if (!printed) {
			printf("%d:\n", pathid);
			path.print();
		}
		printf("-----> nothing matched\n");
	}
	match_tally[tally]++;
}
