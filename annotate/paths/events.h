#ifndef EVENTS_H
#define EVENTS_H

#include <sys/time.h>

typedef enum { EV_HEADER, EV_START_TASK, EV_END_TASK, EV_SET_PATH_ID, EV_END_PATH_ID, EV_NOTICE, EV_SEND, EV_RECV } EventType;

class Event {
public:
	virtual void print(FILE *fp = stdin) = 0;
	virtual ~Event(void) {}
	virtual EventType type(void) = 0;
	timeval tv;
};

class Header : public Event {
public:
	Header(const char *buf);
	virtual ~Header(void);
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_HEADER; }

	int magic, version;
	int tz, pid, tid, ppid, uid;
	char *hostname, *processname;
};

/* abstract event type with rusage information */
class ResourceMark : public Event {
public:
	ResourceMark(const char *buf);

	static const int bufsiz = 11*4;  // 11 ints
	int path_id;
	int minor_fault, major_fault, vol_cs, invol_cs;
	timeval utime, stime;
};

/* abstract event type for start/end task */
class Task : public ResourceMark {
public:
	Task(const char *buf);
	virtual ~Task(void);

	char *name;
};

class StartTask : public Task {
public:
	StartTask(const char *buf) : Task(buf) {}
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_START_TASK; }
};

class EndTask : public Task {
public:
	EndTask(const char *buf) : Task(buf) {}
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_END_TASK; }
};

class NewPathID : public ResourceMark {
public:
	NewPathID(const char *buf);
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_SET_PATH_ID; }
};

class EndPathID : public Event {
public:
	EndPathID(const char *buf);
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_END_PATH_ID; }

	int path_id;
};

class Notice : public Event {
public:
	Notice(const char *buf);
	virtual ~Notice(void);
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_NOTICE; }

	int path_id;
	char *str;
};

/* abstract type for sending/receiving events */
class Message : public Event {
public:
	Message(const char *buf);

	int path_id, sender, msg_id, size;
};

class MessageSend : public Message {
public:
	MessageSend(const char *buf) : Message(buf) { }
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_SEND; }
};

class MessageRecv : public Message {
public:
	MessageRecv(const char *buf) : Message(buf) { }
	virtual void print(FILE *fp);
	virtual EventType type(void) { return EV_RECV; }
};

Event *read_event(FILE *_fp);

#endif
