#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <map>
#include <string>
#include "common.h"
#include "events.h"
#include <mysql/mysql.h>

inline static long long ts(const timeval tv) {
	return 1000000LL*tv.tv_sec + tv.tv_usec;
}
static void run_sql(const char *fmt, ...)
	__attribute__((__format__(printf,1,2)));
struct ltstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1, s2) < 0;
  }
};
static MYSQL mysql;

static void read_file(const char *fn, const std::string &table_tasks,
		const std::string &table_notices, const std::string &table_threads);

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage:\n  %s table-name trace-file [trace-file [...]]\n\n", argv[0]);
		return 1;
	}
	std::string base(argv[1]);
	std::string table_tasks = base + "_tasks";
	std::string table_notices = base + "_notices";
	std::string table_threads = base + "_threads";

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, NULL, "root", NULL, "anno", 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		return 1;
	}
	run_sql("CREATE TABLE %s (pathid int, name varchar(255), ts bigint, "
		"thread_id int)", table_notices.c_str());
	run_sql("CREATE TABLE %s (pathid int, name varchar(255), start bigint, "
		"end bigint, utime int, stime int, major_fault int, minor_fault int, "
		"vol_cs int, invol_cs int, thread_id int)",
		table_tasks.c_str());
	run_sql("CREATE TABLE %s (thread_id int auto_increment primary key, "
		"host varchar(255), prog varchar(255), pid int, tid int, ppid int, uid int, "
		"start bigint, tz int)", table_threads.c_str());

	for (int i=2; i<argc; i++)
		read_file(argv[i], table_tasks, table_notices, table_threads);

	mysql_close(&mysql);
	return 0;
}

static void read_file(const char *fn, const std::string &table_tasks,
		const std::string &table_notices, const std::string &table_threads) {
	FILE *fp = fopen(fn, "r");
	if (!fp) { perror(fn); return; }

	int thread_id = -1;
	std::map<int, std::map<const char *, Event *, ltstr> > start_event;
	Event *e = read_event(fp);

	while (e) {
		switch (e->type()) {
			case EV_HEADER:{
					Header *hdr = (Header*)e;
					run_sql("INSERT INTO %s VALUES (0, '%s', '%s', %d, %d, %d, %d, %lld, %d)",
						table_threads.c_str(), hdr->hostname, hdr->processname, hdr->pid,
						hdr->tid, hdr->ppid, hdr->uid, ts(hdr->tv), hdr->tz);
					thread_id = mysql_insert_id(&mysql);
				}
				delete e;
				break;
			case EV_START_TASK:
				start_event[((Task*)e)->path_id][((Task*)e)->name] = e;
				break;
			case EV_END_TASK:{
					assert(thread_id != -1);
					Task *end = (Task*)e;
					Task *start = (Task*)start_event[end->path_id][end->name];
					run_sql("INSERT INTO %s VALUES "
						"(%d, \"%s\", %lld, %lld, %ld, %ld, %d, %d, %d, %d, %d)",
						table_tasks.c_str(), end->path_id, end->name,
						ts(start->tv), ts(end->tv),
						end->utime - start->utime,
						end->stime - start->stime,
						end->minor_fault - start->minor_fault,
						end->major_fault - start->major_fault,
						end->vol_cs - start->vol_cs,
						end->invol_cs - start->invol_cs,
						thread_id);
					start_event[start->path_id].erase(start->name);
					delete start;
				}
				delete e;
				break;
			case EV_SET_PATH_ID:
				e->print();
				delete e;
				break;
			case EV_END_PATH_ID:
				e->print();
				delete e;
				break;
			case EV_NOTICE:
				assert(thread_id != -1);
				run_sql("INSERT INTO %s VALUES (%d, \"%s\", %lld, %d)",
					table_notices.c_str(), ((Notice*)e)->path_id,
					((Notice*)e)->str, ts(((Notice*)e)->tv),
					thread_id);
				delete e;
				break;
			case EV_SEND:
				e->print();
				delete e;
				break;
			case EV_RECV:
				e->print();
				delete e;
				break;
			default:
				fprintf(stderr, "Invalid event type: %d\n", e->type());
				delete e;
		}
		e = read_event(fp);
	}
}

static void run_sql(const char *fmt, ...) {
	char query[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(query, fmt, arg);
	va_end(arg);
	if (mysql_query(&mysql, query) != 0) {
		printf("Database error:\n");
		printf("  QUERY: \"%s\"\n", query);
		printf("  MySQL error: \"%s\"\n", mysql_error(&mysql));
	}
}
