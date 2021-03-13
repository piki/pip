/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef EVENTS_H
#define EVENTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include "common.h"

typedef enum { EV_HEADER, EV_START_TASK, EV_END_TASK, EV_SET_PATH_ID, EV_END_PATH_ID, EV_NOTICE, EV_SEND, EV_RECV, EV_BELIEF_FIRST, EV_BELIEF } EventType;

struct HashString {
	inline size_t operator()(const std::string &x) const {
		register unsigned long h = 0;
		register const char *p;
		register int l;
		for (p=x.data(),l=x.length(); l; ++p,--l)
		h = 5 * h + *p;
		return size_t(h);
	}
};

class Event {
public:
	virtual void print(FILE *fp = stdout, int depth = 0) = 0;
	virtual ~Event(void) { if (roles) delete[] roles; }
	virtual EventType type(void) = 0;
	bool operator< (const Event &test) const { return tv < test.tv; }
	timeval tv;
	char *roles;
	char level;
};

class Header : public Event {
public:
	Header(const unsigned char *buf);
	virtual ~Header(void);
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_HEADER; }

	int magic, version;
	int tz, pid, tid, ppid, uid;
	char *hostname, *processname;
};

/* abstract event type with rusage information */
class ResourceMark : public Event {
public:
	ResourceMark(int version, const unsigned char *buf);

	inline int bufsiz(int version) const {
		switch (version) {
			case 2: return 10*4;   // 10 ints
			case 3: return 10*4 + 2 + (roles?strlen(roles):0) + 1; // roles + level
			default: fprintf(stderr, "Unknown version %d\n", version); exit(1);
		}
	}
	int minor_fault, major_fault, vol_cs, invol_cs;
	timeval utime, stime;
};

/* abstract event type for start/end task */
class Task : public ResourceMark {
public:
	Task(int version, const unsigned char *buf);
	virtual ~Task(void);

	char *name;
	int thread_id;
	union { int i; void *v; } path_id;
};

class StartTask : public Task {
public:
	StartTask(int version, const unsigned char *buf) : Task(version, buf) {}
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_START_TASK; }
};

class EndTask : public Task {
public:
	EndTask(int version, const unsigned char *buf) : Task(version, buf) {}
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_END_TASK; }
};

class NewPathID : public ResourceMark {
public:
	NewPathID(int version, const unsigned char *buf);
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_SET_PATH_ID; }

	std::string path_id;
};

class EndPathID : public Event {
public:
	EndPathID(int version, const unsigned char *buf);
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_END_PATH_ID; }

	std::string path_id;
};

class Notice : public Event {
public:
	Notice(int version, const unsigned char *buf);
	virtual ~Notice(void);
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_NOTICE; }

	char *str;
};

/* abstract type for sending/receiving events */
class Message : public Event {
public:
	Message(int version, const unsigned char *buf);

	std::string msgid;
	int size, thread_id;
	union { int i; void *v; } path_id;
};

class MessageSend : public Message {
public:
	MessageSend(int version, const unsigned char *buf) : Message(version, buf) { }
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_SEND; }
};

class MessageRecv : public Message {
public:
	MessageRecv(int version, const unsigned char *buf) : Message(version, buf) { }
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_RECV; }
};

class BeliefFirst : public Event {
public:
	BeliefFirst(int version, const unsigned char *buf);
	~BeliefFirst(void);
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_BELIEF_FIRST; }

	int seq;
	float max_fail_rate;
	char *cond, *file;
	int line;
	// no timestamp
};

class Belief : public Event {
public:
	Belief(int version, const unsigned char *buf);
	virtual void print(FILE *fp, int depth);
	virtual EventType type(void) { return EV_BELIEF; }

	int seq;
	bool cond;
};

Event *read_event(int version, FILE *_fp);
Event *parse_event(int version, const unsigned char *buf);
const char *ID_to_string(const std::string &str);

#endif
