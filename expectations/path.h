#ifndef PATH_H
#define PATH_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <map>
#include <utility>
#include <vector>
#include <mysql/mysql.h>
#include <string>

class PathNotice;
class PathMessageSend;
class PathMessageRecv;

enum PathEventType { PEV_TASK, PEV_NOTICE, PEV_MESSAGE_SEND, PEV_MESSAGE_RECV };

class PathEvent {
public:
	virtual ~PathEvent(void) {}
	virtual PathEventType type(void) const = 0; 
	virtual void print(FILE *fp = stdout, int depth = 0) const = 0;
	virtual const timeval &start(void) const = 0;
	virtual const timeval &end(void) const = 0;
};
typedef std::vector<PathEvent *> PathEventList;

class PathTask : public PathEvent {
public:
	PathTask(const MYSQL_ROW &row);
	~PathTask(void);
	virtual PathEventType type(void) const { return PEV_TASK; }
	virtual const timeval &start(void) const { return ts_start; }
	virtual const timeval &end(void) const { return ts_end; }
	void print(FILE *fp = stdout, int depth = 0) const;

	char *name;
	int tdiff, utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;
	int thread_start, thread_end;

	PathEventList children;
};

class PathNotice : public PathEvent {
public:
	PathNotice(const MYSQL_ROW &row);
	~PathNotice(void) { free(name); }
	virtual PathEventType type(void) const { return PEV_NOTICE; }
	virtual const timeval &start(void) const { return ts; }
	virtual const timeval &end(void) const { return ts; }
	void print(FILE *fp = stdout, int depth = 0) const;

	char *name;
	timeval ts;
	int thread_id;
};

class PathMessageSend : public PathEvent {
public:
	PathMessageSend(const MYSQL_ROW &row);
	virtual PathEventType type(void) const { return PEV_MESSAGE_SEND; }
	virtual const timeval &start(void) const { return ts_send; }
	virtual const timeval &end(void) const { return ts_send; }
	void print(FILE *fp = stdout, int depth = 0) const;

	PathEventList *dest;
	PathMessageRecv *pred;   // closest predecessor in current thread
	timeval ts_send;
	int size, thread_send;
};

class PathMessageRecv : public PathEvent {
public:
	PathMessageRecv(const MYSQL_ROW &row);
	virtual PathEventType type(void) const { return PEV_MESSAGE_RECV; }
	virtual const timeval &start(void) const { return ts_recv; }
	virtual const timeval &end(void) const { return ts_recv; }
	void print(FILE *fp = stdout, int depth = 0) const;

	PathMessageSend *send;
	timeval ts_recv;
	int thread_recv;
};

class PathThread {
public:
	PathThread(const MYSQL_ROW &row);
	void print(FILE *fp = stdout, int depth = 0) const;

	int thread_id;
	std::string host, prog;
	int pid, tid, ppid, uid;
	timeval start;
	int tz;
};

class PathMessage {
public:
	PathMessage(const MYSQL_ROW &row) {
		send = new PathMessageSend(row);
		recv = new PathMessageRecv(row);
		recv->send = send;
	}
	PathMessageSend *send;
	PathMessageRecv *recv;
};

class Path {
public:
	Path(void);
	~Path(void);
	// !! thread can start/end in different threads
	void insert(PathTask *pt) { insert_task(pt, children_map[pt->thread_start]); }
	void insert(PathNotice *pn) { insert_event(pn, children_map[pn->thread_id]); }
	void insert(PathMessage *pm) {
		insert_event(pm->send, children_map[pm->send->thread_send]);
		insert_event(pm->recv, children_map[pm->recv->thread_recv]);
		pm->send->dest = &children_map[pm->recv->thread_recv];
		delete pm;  // does not delete its children
	}
	void print(FILE *fp = stdout) const;
	void done_inserting(void);

	PathEventList children;

	int utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;
	int size, path_id;
private:
	void insert_task(PathTask *pt, PathEventList &where);
	void insert_event(PathEvent *pn, PathEventList &where);  // notices, msg-send, msg-recv
	void tally(const PathEventList *list, bool toplevel);

	std::map<int,PathEventList> children_map;
};

extern std::map<int, PathThread*> threads;

#endif
