#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>
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
struct ltIDBlock {
  bool operator()(const IDBlock &id1, const IDBlock &id2) const {
		if (id1.len < id2.len) return true;
		if (id1.len > id2.len) return false;
		return memcmp(id1.data, id2.data, id1.len) < 0;
  }
};
static MYSQL mysql;

static void read_file(const char *fn);
static void reconcile(Message *send, Message *recv, bool is_send, int thread_id, int path_id);
static void check_unpaired_messages(void);

static std::string table_tasks;
static std::string table_notices;
static std::string table_messages;
static std::string table_threads;
static std::string table_paths;
static int errors = 0;

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage:\n  %s table-name trace-file [trace-file [...]]\n\n", argv[0]);
		return 1;
	}
	std::string base(argv[1]);
	table_tasks = base + "_tasks";
	table_notices = base + "_notices";
	table_messages = base + "_messages";
	table_threads = base + "_threads";
	table_paths = base + "_paths";

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
	run_sql("CREATE TABLE %s (pathid int, msgid varchar(255), ts_send bigint, ts_recv bigint, "
		"size int, thread_send int, thread_recv int)", table_messages.c_str());
	run_sql("CREATE TABLE %s (pathid int, pathblob varchar(255))", table_paths.c_str());

	for (int i=2; i<argc; i++)
		read_file(argv[i]);

	mysql_close(&mysql);

	check_unpaired_messages();
	printf("There were %d error%s\n", errors, errors==1?"":"s");
	return errors > 0;
}

typedef std::map<IDBlock, Message*, ltIDBlock> MessageMap;
static std::map<IDBlock, int, ltIDBlock> path_ids;
static MessageMap sends;
static MessageMap receives;
static int next_id = 1;
static void read_file(const char *fn) {
	fprintf(stderr, "Reading %s\n", fn);
	FILE *fp = fopen(fn, "r");
	if (!fp) { perror(fn); return; }

	int thread_id = -1;
	typedef std::vector<Event *> EventList;
	typedef std::map<const char *, EventList, ltstr> NameEventMap;
	typedef std::map<int, NameEventMap> PathNameEventMap;
	PathNameEventMap start_task;
	Event *e = read_event(fp);
	int current_id = -1;
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
			case EV_START_TASK:{
				assert(thread_id != -1);
				EventList *evl = &start_task[current_id][((Task*)e)->name];
				evl->push_back(e);
				break;
				}
			case EV_END_TASK:{
					assert(thread_id != -1);
					Task *end = (Task*)e;
					EventList *evl = &start_task[current_id][end->name];
					if (evl == NULL || evl->empty()) {
						fprintf(stderr, "start not found for...\n  ");
						e->print(stderr);
						abort();
					}
					Task *start = (Task*)evl->back();
					evl->pop_back();
					if (evl->empty()) 
						start_task[current_id].erase(start->name);
					run_sql("INSERT INTO %s VALUES "
						"(%d, \"%s\", %lld, %lld, %ld, %ld, %d, %d, %d, %d, %d)",
						table_tasks.c_str(), current_id, end->name,
						ts(start->tv), ts(end->tv),
						end->utime - start->utime,
						end->stime - start->stime,
						end->minor_fault - start->minor_fault,
						end->major_fault - start->major_fault,
						end->vol_cs - start->vol_cs,
						end->invol_cs - start->invol_cs,
						thread_id);
					delete start;
				}
				delete e;
				break;
			case EV_SET_PATH_ID:
				//e->print();
				if (path_ids.count(((NewPathID*)e)->path_id) == 0) {
					current_id = path_ids[((NewPathID*)e)->path_id] = next_id++;
					run_sql("INSERT INTO %s VALUES (%d, '%s')",
						table_paths.c_str(), current_id, ((NewPathID*)e)->path_id.to_string());
				}
				else
					current_id = path_ids[((NewPathID*)e)->path_id];
				delete e;
				break;
			case EV_END_PATH_ID:
				//e->print();
				delete e;
				break;
			case EV_NOTICE:
				assert(thread_id != -1);
				run_sql("INSERT INTO %s VALUES (%d, \"%s\", %lld, %d)",
					table_notices.c_str(), current_id, ((Notice*)e)->str,
					ts(((Notice*)e)->tv), thread_id);
				delete e;
				break;
			case EV_SEND:
				reconcile((Message*)e, receives.find(((Message*)e)->msgid)->second, true, thread_id, current_id);
				break;
			case EV_RECV:
				reconcile(sends.find(((Message*)e)->msgid)->second, (Message*)e, false, thread_id, current_id);
				break;
			default:
				fprintf(stderr, "Invalid event type: %d\n", e->type());
				delete e;
		}
		e = read_event(fp);
	}

	// print all starts, sends, and receives left in the hash tables
	for (PathNameEventMap::const_iterator pathp=start_task.begin(); pathp!=start_task.end(); pathp++)
		for (NameEventMap::const_iterator namep=pathp->second.begin(); namep!=pathp->second.end(); namep++)
			for (EventList::const_iterator eventp=namep->second.begin(); eventp!=namep->second.end(); eventp++) {
				fprintf(stderr, "Unmatched task start: ");
				(*eventp)->print(stderr);
				errors++;
			}
}

static void check_unpaired_messages() {
	MessageMap::const_iterator msgp;
	fprintf(stderr, "Unmatched send count = %d\n", sends.size());
	for (msgp=sends.begin(); msgp!=sends.end(); msgp++) {
		fprintf(stderr, "Unmatched send: ");
		msgp->second->print(stderr);
		errors++;
	}
	fprintf(stderr, "Unmatched recv count = %d\n", receives.size());
	for (msgp=receives.begin(); msgp!=receives.end(); msgp++) {
		fprintf(stderr, "Unmatched recv: ");
		msgp->second->print(stderr);
		errors++;
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

static void reconcile(Message *send, Message *recv, bool is_send, int thread_id, int path_id) {
	assert(thread_id != -1);
	if (is_send)
		send->thread_id = thread_id;
	else
		recv->thread_id = thread_id;
	if (send && recv) {
		if (send->size != recv->size) {
			fprintf(stderr, "packet size mismatch:\n  "); send->print(stderr);
			fprintf(stderr, "  "); recv->print(stderr);
			//abort();
			errors++;
		}
		run_sql("INSERT INTO %s VALUES (%d, '%s', %lld, %lld, %d, %d, %d)",
			table_messages.c_str(), path_id, send->msgid.to_string(), ts(send->tv), ts(recv->tv),
			send->size, send->thread_id, recv->thread_id);
		if (is_send)
			receives.erase(recv->msgid);
		else
			sends.erase(send->msgid);
		delete recv;
		delete send;
	}
	else {
		MessageMap &table = is_send ? sends : receives;
		Message *msg = is_send ? send : recv;
		if (table[msg->msgid] != NULL) {
			fprintf(stderr, "Reused message id:\n  OLD: "); table[msg->msgid]->print(stderr);
			fprintf(stderr, "  NEW: "); msg->print(stderr);
			//abort();
			errors++;
		}
		table[msg->msgid] = msg;
	}
}
