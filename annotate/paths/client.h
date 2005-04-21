#ifndef CLIENT_H
#define CLIENT_H

#include <map>
#include <string>
#include <vector>
#include "events.h"
#include "reconcile.h"

class Client {
public:
	Client(void);
	~Client(void) { if (header) delete header; }
	void append(const char *newbuf, int len);
	void end(void);

	int handle;

private:
	Event *get_event(void);
	void handle_event(Event *ev);

	char *buf;
	int bufhead, buflen, bufsiz;

	Header *header;
	int thread_id, current_id;
	PathNameTaskMap start_task;  // stack of start events for <pathid, name>
};

#endif
