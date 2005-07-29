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
	virtual int compare(const PathEvent *other) const;
	virtual const timeval &start(void) const { return ts_start; }
	virtual const timeval &end(void) const { return ts_end; }
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
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
	virtual int compare(const PathEvent *other) const;
	virtual const timeval &start(void) const { return ts; }
	virtual const timeval &end(void) const { return ts; }
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, int depth = 0) const;

	char *name;
	timeval ts;
	int thread_id;
};

class PathMessageSend : public PathEvent {
public:
	PathMessageSend(const MYSQL_ROW &row);
	virtual PathEventType type(void) const { return PEV_MESSAGE_SEND; }
	virtual int compare(const PathEvent *other) const;
	virtual const timeval &start(void) const { return ts_send; }
	virtual const timeval &end(void) const { return ts_send; }
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, int depth = 0) const;

	PathEventList *dest;
	PathMessageRecv *pred;   // closest predecessor in current thread
	PathMessageRecv *recv;
	timeval ts_send;
	int size, thread_send;
};

class PathMessageRecv : public PathEvent {
public:
	PathMessageRecv(const MYSQL_ROW &row);
	virtual PathEventType type(void) const { return PEV_MESSAGE_RECV; }
	virtual int compare(const PathEvent *other) const;
	virtual const timeval &start(void) const { return ts_recv; }
	virtual const timeval &end(void) const { return ts_recv; }
	virtual std::string to_string(void) const;
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout, int depth = 0) const;

	PathMessageSend *send;
	timeval ts_recv;
	int thread_recv;
};

class PathThread {
public:
	PathThread(const MYSQL_ROW &row);
	void print_dot(FILE *fp = stdout) const;
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
		send->recv = recv;
	}
	PathMessageSend *send;
	PathMessageRecv *recv;
};

class Path {
public:
	Path(void);
	Path(MYSQL *mysql, const char *base, int _path_id);
	~Path(void);
	void init(void);
	void read(MYSQL *mysql, const char *base, int _path_id);
	inline bool valid(void) const { return root_thread != -1; }
	int compare(const Path &other) const;
	// !! thread can start/end in different threads
	void insert(PathTask *pt) { insert_task(pt, children[pt->thread_start]); }
	void insert(PathNotice *pn) { insert_event(pn, children[pn->thread_id]); }
	void insert(PathMessage *pm) {
		insert_event(pm->send, children[pm->send->thread_send]);
		insert_event(pm->recv, children[pm->recv->thread_recv]);
		pm->send->dest = &children[pm->recv->thread_recv];
		delete pm;  // does not delete its children
	}
	void print_dot(FILE *fp = stdout) const;
	void print(FILE *fp = stdout) const;
	void done_inserting(void);

	std::map<int,PathEventList> children;
	int utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;
	int size, messages, depth, hosts;
	int path_id;
	int root_thread;
private:
	void insert_task(PathTask *pt, PathEventList &where);
	void insert_event(PathEvent *pn, PathEventList &where);  // notices, msg-send, msg-recv
	void tally(const PathEventList &list, bool toplevel);
};

extern std::map<int, PathThread*> threads;

timeval ts_to_tv(long long ts);
void get_path_ids(MYSQL *mysql, const char *table_base, std::set<int> *pathids);
void get_threads(MYSQL *mysql, const char *table_base, std::map<int, PathThread*> *threads);
void run_sql(MYSQL *mysql, const char *cmd);
void run_sqlf(MYSQL *mysql, const char *fmt, ...)
	__attribute__((__format__(printf,2,3)));

#endif
