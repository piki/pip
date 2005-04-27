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
extern MYSQL mysql;

Path *read_path(const char *base, int pathid) {
	Path *path = new Path();
	std::map<std::string,Recognizer*>::const_iterator rp;

	path->path_id = pathid;

	run_sqlf(&mysql, "SELECT * FROM %s_tasks WHERE pathid=%d ORDER BY start", base, pathid);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathTask *pt = new PathTask(row);
		path->insert(pt);
	}
	mysql_free_result(res);

	run_sqlf(&mysql, "SELECT * FROM %s_notices WHERE pathid=%d", base, pathid);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathNotice *pn = new PathNotice(row);
		path->insert(pn);
	}
	mysql_free_result(res);

	run_sqlf(&mysql, "SELECT * FROM %s_messages WHERE pathid=%d", base, pathid);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathMessage *pm = new PathMessage(row);
		path->insert(pm);
	}
	mysql_free_result(res);

	path->done_inserting();
	if (!path->valid()) {
		printf("# path %d malformed -- not checked\n", pathid);
		delete path;
		return NULL;
	}
	printf("# path %d\n", pathid);
	path->print();
	putchar('\n');
	return path;
}
