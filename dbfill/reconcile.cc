/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <stdarg.h>
#include <map>
#include <set>
#include <string>
#include <mysql/mysql.h>
#include "client.h"
#include "events.h"
#include "insertbuffer.h"
#include "rcfile.h"
#include "reconcile.h"

// globals
MYSQL mysql;
std::string table_tasks;
std::string table_notices;
std::string table_messages;
std::string table_threads;
std::string table_paths;
int errors = 0;
std::map<std::string, std::set<Task*, ltEvP> > unpaired_tasks;
MessageMap sends;
MessageMap receives;
bool save_unmatched_sends = false;

static void check_unpaired_tasks(void);
static void check_unpaired_messages(void);

long long tv_to_ts(const timeval tv) {
	return 1000000LL*tv.tv_sec + tv.tv_usec;
}

void reconcile_init(const char *table_base) {
	std::string base(table_base);
	table_tasks = base + "_tasks";
	table_notices = base + "_notices";
	table_messages = base + "_messages";
	table_threads = base + "_threads";
	table_paths = base + "_paths";

	mysql_init(&mysql);
	if (!mysql_real_connect(&mysql, config["db.host"], config.get("db.user", getlogin()),
			config["db.password"], NULL, 0, NULL, 0)) {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		exit(1);
	}

	// does the "pip" database exist?  if not, create it.
	run_sql("SHOW DATABASES");
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	bool found = false;
	const char *dbname = config.get("db.name", "pip");
	while ((row = mysql_fetch_row(res)) != NULL)
		if (!strcmp(row[0], dbname)) found = true;
	mysql_free_result(res);
	if (!found) {
		fprintf(stderr, "Creating database \"%s\"\n", dbname);
		run_sqlf("CREATE DATABASE %s", dbname);
	}

	run_sqlf("USE %s", dbname);
	run_sqlf("CREATE TABLE %s (pathid int, roles varchar(255), level tinyint, "
		"name varchar(255), ts bigint, thread_id int, INDEX(pathid))",
		table_notices.c_str());
	run_sqlf("CREATE TABLE %s (pathid int, roles varchar(255), level tinyint, "
		"name varchar(255), start bigint, end bigint, tdiff int, utime int, "
		"stime int, major_fault int, minor_fault int, vol_cs int, invol_cs int, "
		"thread_start int, thread_end int, INDEX(pathid), INDEX(name))",
		table_tasks.c_str());
	run_sqlf("CREATE TABLE %s (thread_id int auto_increment primary key, "
		"host varchar(255), prog varchar(255), pid int, tid int, ppid int, "
		"uid int, start bigint, tz int)", table_threads.c_str());
	run_sqlf("CREATE TABLE %s (pathid int, roles varchar(255), levels tinyint, "
		"msgid varchar(255), ts_send bigint, ts_recv bigint, size int, "
		"thread_send int, thread_recv int, INDEX(pathid))",
		table_messages.c_str());
	run_sqlf("CREATE TABLE %s (pathid int primary key, pathblob varchar(255))",
		table_paths.c_str());
	run_sqlf("LOCK TABLES %s WRITE, %s WRITE, %s WRITE, %s WRITE, %s WRITE",
		table_notices.c_str(), table_tasks.c_str(), table_threads.c_str(),
		table_messages.c_str(), table_paths.c_str());
}

void reconcile_done(void) {
	check_unpaired_tasks();
	check_unpaired_messages();
	SqlBuffer::flush_all();
	run_sql("UNLOCK TABLES");
	mysql_close(&mysql);
}

void run_sqlf(const char *fmt, ...) {
	char query[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(query, fmt, arg);
	va_end(arg);
	run_sql(query);
}

void run_sql(const char *cmd) {
	if (mysql_query(&mysql, cmd) != 0) {
		char buf[256];
		strncpy(buf, cmd, 256);
		buf[255] = '\0';
		printf("Database error:\n");
		printf("  QUERY: \"%s\"\n", buf);
		printf("  MySQL error: \"%s\"\n", mysql_error(&mysql));
		exit(1);
	}
}

bool handle_end_task(Task *end, PathNameTaskMap &start_task) {
	//!! use find() so it doesn't auto-create entire vectors
	StartList *evl = &start_task[end->path_id.i][end->name];
	if (evl == NULL || evl->empty())
		return false;
	Task *start = (Task*)evl->back();
	evl->pop_back();
	assert(start->path_id.i == end->path_id.i);
	if (evl->empty()) start_task[end->path_id.i].erase(start->name);
	SqlBuffer::insert(table_tasks, "(%d,\"%s\",%d,\"%s\",%lld,%lld,%ld,%ld,%ld,%d,%d,%d,%d,%d,%d)",
		start->path_id.i,
		start->roles ? start->roles : "", start->level,
		end->name,
		tv_to_ts(start->tv), tv_to_ts(end->tv),
		end->tv - start->tv,         // !! wrong, doesn't account for switchage
		end->utime - start->utime,
		end->stime - start->stime,
		end->minor_fault - start->minor_fault,
		end->major_fault - start->major_fault,
		end->vol_cs - start->vol_cs,
		end->invol_cs - start->invol_cs,
		start->thread_id, end->thread_id);
	delete start;
	delete end;
	return true;
}

// print all tasks starts and ends left in the hash table
static void check_unpaired_tasks(void) {
	std::map<std::string, std::set<Task*, ltEvP> >::const_iterator tasksetp;
	for (tasksetp=unpaired_tasks.begin(); tasksetp!=unpaired_tasks.end(); tasksetp++) {
		// host is tasksetp->first
		// set of unmatched tasks (starts and ends together) is tasksetp->second
		PathNameTaskMap start_task;
		for (std::set<Task*, ltEvP>::const_iterator taskp=tasksetp->second.begin(); taskp!=tasksetp->second.end(); taskp++) {
			Task *ev = (Task*)(*taskp);
			assert(ev);
			switch (ev->type()) {
				case EV_START_TASK:{
						StartList *evl = &start_task[ev->path_id.i][ev->name];
						evl->push_back((StartTask*)ev);
					}
					break;
				case EV_END_TASK:{
						if (!handle_end_task((EndTask*)ev, start_task)) {
							fprintf(stderr, "task end without start on %s, path %d: ", tasksetp->first.c_str(), ev->path_id.i);
							ev->print(stderr);
							errors++;
						}
					}
					break;
				default:
					fprintf(stderr, "Unexpected event type in unpaired_tasks table: %d\n", ev->type());
					abort();
			}
			//fprintf(stderr, "Unpaired task on %s: ", tasksetp->first.c_str()); ev->print(stderr);
		}

		// now let's see what's left in start_task
		for (PathNameTaskMap::const_iterator pathp=start_task.begin(); pathp!=start_task.end(); pathp++)
			for (NameTaskMap::const_iterator namep=pathp->second.begin(); namep!=pathp->second.end(); namep++)
				for (StartList::const_iterator eventp=namep->second.begin(); eventp!=namep->second.end(); eventp++) {
					fprintf(stderr, "task start without end on %s, path %d:\n", tasksetp->first.c_str(), (*eventp)->path_id.i);
					(*eventp)->print(stderr, 0);
					errors++;
				}
	}
}

// print all sends and receives left in the hash table
static void check_unpaired_messages(void) {
	MessageMap::const_iterator msgp;

	fprintf(stderr, "Unmatched send count = %zd\n", sends.size());
	if (save_unmatched_sends) {
		for (msgp=sends.begin(); msgp!=sends.end(); msgp++) {
			SqlBuffer::insert(table_messages, "(%d,\"%s\",%d,'%s',%lld,NULL,%d,%d,NULL)",
			msgp->second->path_id.i,
			msgp->second->roles ? msgp->second->roles : "", msgp->second->level,
			ID_to_string(msgp->second->msgid), tv_to_ts(msgp->second->tv), /* no recv time */
			msgp->second->size, msgp->second->thread_id /* no recv thread */);
		}
	}
	else {
		for (msgp=sends.begin(); msgp!=sends.end(); msgp++) {
			fprintf(stderr, "Unmatched send: ");
			msgp->second->print(stderr);
			errors++;
		}
	}

	fprintf(stderr, "Unmatched recv count = %zd\n", receives.size());
	for (msgp=receives.begin(); msgp!=receives.end(); msgp++) {
		fprintf(stderr, "Unmatched recv: ");
		msgp->second->print(stderr);
		errors++;
	}
}
