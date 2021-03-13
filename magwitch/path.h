/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef PATH_H
#define PATH_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include <mysql/mysql.h>
#include <string>

class PathNotice;
class PathMessageSend;
class PathMessageRecv;

enum PathEventType { PEV_TASK=0, PEV_NOTICE, PEV_MESSAGE_SEND, PEV_MESSAGE_RECV };
extern const char *path_type_name[];
enum PathCompareResult { PCMP_EXACT=0, PCMP_NAMES, PCMP_NONE };

class PathEvent {
public:
	virtual ~PathEvent(void) {}
	virtual PathEventType type(void) const = 0; 
	virtual int compare(const PathEvent *other) const = 0;
	virtual std::string to_string(void) const = 0;
	virtual void print_dot(FILE *fp = stdout) const = 0;
	virtual void print(FILE *fp = stdout, unsigned int depth = 0) const = 0;
	virtual void print_exp(FILE *fp = stdout, unsigned int depth = 0) const = 0;
	inline const timeval &start(void) const { return ts; }
	virtual const timeval &end(void) const { return ts; }
	virtual bool operator<(const PathEvent &other) const { return cmp(&other) < 0; }
	virtual bool operator==(const PathEvent &other) const { return cmp(&other) == 0; }
	virtual bool operator!=(const PathEvent &other) const { return cmp(&other) != 0; }
	virtual int cmp(const PathEvent *other) const = 0;
	int level, path_id, thread_id;
	timeval ts;
};
typedef std::vector<PathEvent *> PathEventList;

class PathTask : public PathEvent {
public:
	PathTask(const MYSQL_ROW &row);
	~PathTask(void);
	virtual PathEventType type(void) const { return PEV_TASK; }
	virtual int compare(const PathEvent *other) const;
	virtual const timeval &end(void) const { return ts_end; }
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, unsigned int depth = 0) const;
	void print_exp(FILE *fp = stdout, unsigned int depth = 0) const;
	virtual int cmp(const PathEvent *other) const;

	char *name;
	int tdiff, utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_end;

	PathEventList children;
};

class PathNotice : public PathEvent {
public:
	PathNotice(const MYSQL_ROW &row);
	~PathNotice(void) { free(name); }
	virtual PathEventType type(void) const { return PEV_NOTICE; }
	virtual int compare(const PathEvent *other) const;
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, unsigned int depth = 0) const;
	void print_exp(FILE *fp = stdout, unsigned int depth = 0) const;
	virtual int cmp(const PathEvent *other) const;

	char *name;
};

class PathMessageSend : public PathEvent {
public:
	PathMessageSend(const MYSQL_ROW &row);
	virtual PathEventType type(void) const { return PEV_MESSAGE_SEND; }
	virtual int compare(const PathEvent *other) const;
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, unsigned int depth = 0) const;
	void print_exp(FILE *fp = stdout, unsigned int depth = 0) const;
	virtual int cmp(const PathEvent *other) const;

	PathEventList *dest;
	PathMessageRecv *pred;   // closest predecessor in current thread
	PathMessageRecv *recv;
	int size;
};

class PathMessageRecv : public PathEvent {
public:
	PathMessageRecv(const MYSQL_ROW &row);
	virtual PathEventType type(void) const { return PEV_MESSAGE_RECV; }
	virtual int compare(const PathEvent *other) const;
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, unsigned int depth = 0) const;
	void print_exp(FILE *fp = stdout, unsigned int depth = 0) const;
	virtual int cmp(const PathEvent *other) const;

	PathMessageSend *send;
};

class PathThread {
public:
	PathThread(const MYSQL_ROW &row);
	void print(FILE *fp = stdout, unsigned int depth = 0) const;

	int thread_id;
	std::string host, prog;
	int pid, tid, ppid, uid;
	int pool;
	timeval start;
	int tz;
};

class PathMessage {
public:
	PathMessage(const MYSQL_ROW &row) {
		send = new PathMessageSend(row);
		if (row[5] && row[8]) {
			recv = new PathMessageRecv(row);
			recv->send = send;
		}
		else
			recv = NULL;
		send->recv = recv;
	}
	PathMessageSend *send;
	PathMessageRecv *recv;
};

extern std::map<int, PathThread*> threads;
extern std::map<int, std::vector<PathThread*> > thread_pools;

class Path {
public:
	Path(void);
	Path(MYSQL *mysql, const char *base, int _path_id);
	~Path(void);
	void init(void);
	void read(MYSQL *mysql, const char *base, int _path_id);
	inline bool valid(void) const { return root_thread != -1; }
	int compare(const Path &other) const;
	inline PathEventList &get_events(int tid) { return thread_pools[threads[tid]->pool]; }
	// to get the foo_threads TID from our thread_pool, we have to look at
	// the first event's thread_id.  The list should never be empty (or it
	// wouldn't be in the thread_pools map), but assert to be sure.
	inline static int get_thread_id(const PathEventList &list) { assert(!list.empty()); return list[0]->thread_id; }
	inline int get_thread_id(int thread_pool) const { return get_thread_id(thread_pools.find(thread_pool)->second); }
	inline const PathEventList &get_events(int tid) const { return thread_pools.find(threads[tid]->pool)->second; }
	// !! thread can start/end in different threads
	inline void insert(PathTask *pt) { insert_task(pt, get_events(pt->thread_id)); }
	inline void insert(PathNotice *pn) { insert_event(pn, get_events(pn->thread_id)); }
	inline void insert(PathMessage *pm) {
		insert_event(pm->send, get_events(pm->send->thread_id));
		if (pm->recv) {
			insert_event(pm->recv, get_events(pm->recv->thread_id));
			pm->send->dest = &get_events(pm->recv->thread_id);
		}
		delete pm;  // does not delete its children
	}
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout) const;
	void print_exp(FILE *fp = stdout) const;
	void done_inserting(void);

	std::map<int,PathEventList> thread_pools;
	int utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;
	int size, messages, depth, hosts, latency;
	int path_id;
	int root_thread;
private:
	void insert_task(PathTask *pt, PathEventList &where);
	void insert_event(PathEvent *pn, PathEventList &where);  // notices, msg-send, msg-recv
	void tally(const PathEventList &list, bool toplevel);
};

timeval ts_to_tv(long long ts);
void get_path_ids(MYSQL *mysql, const char *table_base, std::set<int> *pathids);
void get_threads(MYSQL *mysql, const char *table_base, std::map<int, PathThread*> *threads);
void run_sql(MYSQL *mysql, const char *cmd);
void run_sqlf(MYSQL *mysql, const char *fmt, ...)
	__attribute__((__format__(printf,2,3)));

#endif
