#ifndef PATH_H
#define PATH_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include <mysql/mysql.h>

class PathNotice;
class PathMessage;

enum PathEventType { PEV_TASK, PEV_NOTICE, PEV_MESSAGE };

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
	PathTask(const char *_name, const timeval &_ts_start, const timeval &_ts_end,
		int _utime, int _stime, int _major_fault, int _minor_fault, int _vol_cs,
		int _invol_cs) :
		name(strdup(_name)), utime(_utime), stime(_stime),
		major_fault(_major_fault), minor_fault(_minor_fault), vol_cs(_vol_cs),
		invol_cs(_invol_cs), ts_start(_ts_start), ts_end(_ts_end) {}
	PathTask(const MYSQL_ROW &row);
	~PathTask(void);
	virtual PathEventType type(void) const { return PEV_TASK; }
	virtual const timeval &start(void) const { return ts_start; }
	virtual const timeval &end(void) const { return ts_end; }
	void print(FILE *fp = stdout, int depth = 0) const;

	char *name;
	int utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;

	PathEventList children;
};

class PathNotice : public PathEvent {
public:
	PathNotice(const char *_name, const timeval &_ts) :
		name(strdup(_name)), ts(_ts) {}
	PathNotice(const MYSQL_ROW &row);
	~PathNotice(void) { free(name); }
	virtual PathEventType type(void) const { return PEV_NOTICE; }
	virtual const timeval &start(void) const { return ts; }
	virtual const timeval &end(void) const { return ts; }
	void print(FILE *fp = stdout, int depth = 0) const;

	char *name;
	timeval ts;
};

class PathMessage : public PathEvent {
public:
	PathMessage(const timeval &_ts_send, const timeval &_ts_recv, int _size) :
		recip(NULL), ts_send(_ts_send), ts_recv(_ts_recv), size(_size) {}
	~PathMessage(void) { if (recip) delete recip; }
	virtual PathEventType type(void) const { return PEV_MESSAGE; }
	virtual const timeval &start(void) const { return ts_send; }
	virtual const timeval &end(void) const { return ts_send; }
	// end() returns ts_send rather than ts_recv because messages are
	// children of their senders, sorted by send timestamp only
	void print(FILE *fp = stdout, int depth = 0) const;

	PathTask *recip;
	timeval ts_send, ts_recv;
	int size;
};

class Path {
public:
	Path(void);
	~Path(void);
	void insert(PathTask *pt) { insert(pt, children); }
	void insert(PathNotice *pn) { insert(pn, children); }
	void insert(PathMessage *pm) { insert(pm, children); }
	void print(FILE *fp = stdout) const;
	void done_inserting(void);

	PathEventList children;

	int utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;
	int size;
private:
	void insert(PathTask *pt, PathEventList &where);
	void insert(PathNotice *pn, PathEventList &where);
	void insert(PathMessage *pm, PathEventList &where);
	void tally(const PathEventList *list);
};

#endif
