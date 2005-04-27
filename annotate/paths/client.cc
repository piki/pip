#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <mysql/mysql.h>
#include "client.h"
#include "events.h"
#include "reconcile.h"

#define MAX(a,b) ({ typeof(a) _a=(a); typeof(b) _b=(b); _a<_b?_b:_a; })

static std::map<IDBlock, int> path_ids;

static void reconcile(Message *send, Message *recv, bool is_send, int thread_id, int path_id);

Client::Client(void) : handle(-1), buf(NULL), bufhead(0), buflen(0),
		bufsiz(0), header(NULL), thread_id(-1), current_id(-1) { }

void Client::append(const char *newbuf, int len) {
	assert(bufhead == 0);
	if (buflen + len > bufsiz) {
		bufsiz = MAX(bufsiz*2, buflen + len);
		buf = (char*)realloc(buf, bufsiz);
	}
	memcpy(buf+buflen, newbuf, len);
	buflen += len;

	Event *ev;
	while ((ev = get_event()) != NULL) {
		handle_event(ev);
	}
	if (bufhead != 0) {
		memmove(buf, buf+bufhead, buflen);
		bufhead = 0;
	}
}

Event *Client::get_event(void) {
	if (buflen < 2) return NULL;
	int len = (buf[bufhead] << 8) + buf[bufhead+1];
	if (buflen < len) return NULL;
	Event *ret = parse_event(header ? header->version : -1, buf+bufhead+2);
	bufhead += len;
	buflen -= len;
	return ret;
}

static int next_id = 1;
void Client::handle_event(Event *ev) {
	assert(ev);

	switch (ev->type()) {
		case EV_HEADER:
			assert(!header);
			header = (Header*)ev;
			run_sql("INSERT INTO %s VALUES (0, '%s', '%s', %d, %d, %d, %d, %lld, %d)",
				table_threads.c_str(), header->hostname, header->processname, header->pid,
				header->tid, header->ppid, header->uid, tv_to_ts(header->tv), header->tz);
			thread_id = mysql_insert_id(&mysql);
			break;
		case EV_START_TASK:{
				assert(thread_id != -1);
				StartTask *task = (StartTask*)ev;
				task->thread_id = thread_id;
				task->path_id = current_id;
				StartList *evl = &start_task[current_id][task->name];
				evl->push_back(task);
			}
			break;
		case EV_END_TASK:{
				assert(thread_id != -1);
				EndTask *task = (EndTask*)ev;
				task->thread_id = thread_id;
				task->path_id = current_id;
				if (!handle_end_task(task, start_task)) {
					assert(header);
					unpaired_tasks[header->hostname].insert(task);
					break;
				}
			}
			break;
		case EV_SET_PATH_ID:
			//!! if a task is open, stop billing it
			//  anything in path_ids[the current id] is active and should be
			//  paused.
			//ev->print();
			if (path_ids.count(((NewPathID*)ev)->path_id) == 0) {
				current_id = path_ids[((NewPathID*)ev)->path_id] = next_id++;
				run_sql("INSERT INTO %s VALUES (%d, '%s')",
					table_paths.c_str(), current_id, ((NewPathID*)ev)->path_id.to_string());
			}
			else
				current_id = path_ids[((NewPathID*)ev)->path_id];
			delete ev;
			break;
		case EV_END_PATH_ID:
			//ev->print();
			delete ev;
			break;
		case EV_NOTICE:{
			Notice *n = (Notice*)ev;
			assert(thread_id != -1);
			run_sql("INSERT INTO %s VALUES (%d, \"%s\", %d, \"%s\", %lld, %d)",
				table_notices.c_str(), current_id, n->roles ? n->roles : "", n->level,
				n->str, tv_to_ts(n->tv), thread_id);
			delete ev;
			break;
			}
		case EV_SEND:
			reconcile((Message*)ev, receives.find(((Message*)ev)->msgid)->second, true, thread_id, current_id);
			break;
		case EV_RECV:
			reconcile(sends.find(((Message*)ev)->msgid)->second, (Message*)ev, false, thread_id, current_id);
			break;
		case EV_BELIEF_FIRST:
		case EV_BELIEF:
		default:
			fprintf(stderr, "Invalid event type: %d\n", ev->type());
			delete ev;
	}
}

void Client::end(void) {
	// put all starts left in start_task into unpaired_tasks to be checked later
	if (!header) {
		fprintf(stderr, "%s: no header -- zero-length log file?\n", "fn");
		errors++;
	}
	for (PathNameTaskMap::const_iterator pathp=start_task.begin(); pathp!=start_task.end(); pathp++)
		for (NameTaskMap::const_iterator namep=pathp->second.begin(); namep!=pathp->second.end(); namep++)
			for (StartList::const_iterator eventp=namep->second.begin(); eventp!=namep->second.end(); eventp++)
				unpaired_tasks[header->hostname].insert(*eventp);
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
		run_sql("INSERT INTO %s VALUES (%d, \"%s\", %d, '%s', %lld, %lld, %d, %d, %d)",
			table_messages.c_str(), path_id,
			send->roles ? send->roles : "", send->level,
			send->msgid.to_string(), tv_to_ts(send->tv), tv_to_ts(recv->tv),
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
