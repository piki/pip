#include <assert.h>
#include "common.h"
#include "path.h"

#undef PRINT_DOT

std::map<int, PathThread*> threads;

inline static timeval tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}

PathTask::PathTask(const MYSQL_ROW &row) {
	// pathid = atoi(row[0]);
	name = strdup(row[1]);
	ts_start = tv(strtoll(row[2], NULL, 10));
	ts_end = tv(strtoll(row[3], NULL, 10));
	tdiff = atoi(row[4]);
	utime = atoi(row[5]);
	stime = atoi(row[6]);
	major_fault = atoi(row[7]);
	minor_fault = atoi(row[8]);
	vol_cs = atoi(row[9]);
	invol_cs = atoi(row[10]);
	thread_start = atoi(row[11]);
	thread_end = atoi(row[11]);
}

void PathTask::print(FILE *fp, int depth) const {
#ifdef PRINT_DOT
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, depth+1);
#else
	bool empty = children.size() == 0;

	fprintf(fp, "%*s<task name=\"%s\" start=\"%ld.%06ld\" end=\"%ld.%06ld\"%s",
		depth*2, "", name, ts_start.tv_sec, ts_start.tv_usec,
		ts_end.tv_sec, ts_end.tv_usec, empty ? " />\n" : ">\n");
	if (!empty) {
		for (unsigned int i=0; i<children.size(); i++)
			children[i]->print(fp, depth+1);
		fprintf(fp, "%*s</task>\n", depth*2, "");
	}
#endif
}

PathTask::~PathTask(void) {
	free(name);
	for (unsigned int i=0; i<children.size(); i++) delete children[i];
}

PathNotice::PathNotice(const MYSQL_ROW &row) {
	// pathid = atoi(row[0]);
	name = strdup(row[1]);
	ts = tv(strtoll(row[2], NULL, 10));
	thread_id = atoi(row[3]);
}

void PathNotice::print(FILE *fp, int depth) const {
#ifndef PRINT_DOT
	fprintf(fp, "%*s<notice name=\"%s\" ts=\"%ld.%06ld\" />\n", depth*2, "",
		name, ts.tv_sec, ts.tv_usec);
#endif
}

PathMessageSend::PathMessageSend(const MYSQL_ROW &row) : dest(NULL), pred(NULL) {
	// pathid = atoi(row[0]);
	// msgid is row[1]
	ts_send = tv(strtoll(row[2], NULL, 10));
	// ts_recv is row[3]
	size = atoi(row[4]);
	thread_send = atoi(row[5]);
	// thread_recv is row[6]
}

void PathMessageSend::print(FILE *fp, int depth) const {
#ifdef PRINT_DOT
	if (!pred) {
		PathThread *t = threads[thread_send];
		fprintf(fp, "s%x [label = \"%d:%s/%s/%d\"];\n",
			(int)this, thread_send, t->host.c_str(), t->prog.c_str(), t->tid);
	}
	//fprintf(fp, "r%x -> s%x;\n", (int)pred, (int)this);
#else
	fprintf(fp, "%*s<message_send size=%d thread=%d ts=\"%ld.%06ld\" addr=\"%p\"/>\n",
		depth*2, "", size, thread_send, ts_send.tv_sec, ts_send.tv_usec, this);
#endif
}

PathMessageRecv::PathMessageRecv(const MYSQL_ROW &row) : send(NULL) {
	// pathid = atoi(row[0]);
	// msgid is row[1]
	// ts_send is row[2]
	ts_recv = tv(strtoll(row[3], NULL, 10));
	// size is row[4]
	// thread_start is row[5]
	thread_recv = atoi(row[6]);
}

void PathMessageRecv::print(FILE *fp, int depth) const {
#ifdef PRINT_DOT
	PathThread *t = threads[thread_recv];
	fprintf(fp, "r%x [label = \"%d:%s/%s/%d\"];\n",
		(int)this, thread_recv, t->host.c_str(), t->prog.c_str(), t->tid);
	if (send->pred)
		fprintf(fp, "r%x -> r%x;\n", (int)send->pred, (int)this);
	else
		fprintf(fp, "s%x -> r%x;\n", (int)send, (int)this);
#else
	fprintf(fp, "%*s<message_recv thread=%d ts=\"%ld.%06ld\" send=\"%p\" />\n",
		depth*2, "", thread_recv, ts_recv.tv_sec, ts_recv.tv_usec, send);
#endif
}

PathThread::PathThread(const MYSQL_ROW &row) {
	thread_id = atoi(row[0]);
	host = row[1];
	prog = row[2];
	pid = atoi(row[3]);
	tid = atoi(row[4]);
	ppid = atoi(row[5]);
	uid = atoi(row[6]);
	start = tv(strtoll(row[7], NULL, 10));
	tz = atoi(row[8]);
}

void PathThread::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<thread id=%d host=\"%s\" prog=\"%s\" pid=%d tid=%d ppid=%d "
		"uid=%d start=\"%ld.%06ld\" tz=%d />\n", thread_id, host.c_str(),
		prog.c_str(), pid, tid, ppid, uid, start.tv_sec, start.tv_usec, tz);
}

Path::Path(void) : utime(0), stime(0), major_fault(0), minor_fault(0),
		vol_cs(0), invol_cs(0), size(0) {
	ts_start.tv_sec = ts_start.tv_usec = 0;
	ts_end.tv_sec = ts_end.tv_usec = 0;
}

Path::~Path(void) {
	for (unsigned int i=0; i<children.size(); i++)
		delete children[i];
}

enum OverlapType { OV_BEFORE, OV_START, OV_WITHIN, OV_END, OV_AFTER, OV_SPAN, OV_INVALID };
/*        |----A----|
 * |-1-||-2-||-3-||-4-||-5-|
 *       |-----6-----|
 * we assume (could assert) that a_start <= a_end, b_start <= b_end */
static OverlapType tvcmp(const PathEvent *eva, const PathEvent *evb) {
	if (evb->end() <= eva->start()) return OV_BEFORE;
	if (evb->start() >= eva->end()) return OV_AFTER;
	
	int pos_b_start = 10, pos_b_end = 10;
	if (evb->start() <= eva->start()) pos_b_start = -1; else pos_b_start = 0;
	if (evb->end() <= eva->end()) pos_b_end = 0; else pos_b_end = 1;
	switch (pos_b_start) {
		case -1:
			switch (pos_b_end) {
				case 0:    return OV_START;
				case 1:    return OV_SPAN;
				default:   assert(!"invalid b end");
			}
		case 0:
			switch (pos_b_end) {
				case 0:    return OV_WITHIN;
				case 1:    return OV_END;
				default:   assert(!"invalid b end");
			}
		default:
			assert(!"invalid b start");
	}
	assert(!"not reached");
	return OV_INVALID;  // quell warning
}

void Path::insert_task(PathTask *pt, std::vector<PathEvent *> &where) {
	// !! really only need to compare against last child
	for (unsigned int i=0; i<where.size(); i++) {
		switch (tvcmp(where[i], pt)) {
			case OV_START:
				assert(!"New PathTask overlaps start of existing");
			case OV_END:
				assert(!"New PathTask overlaps end of existing");
			case OV_BEFORE:
				where.insert(where.begin()+i, pt);
				return;
			case OV_WITHIN:
				assert(where[i]->type() == PEV_TASK);
				insert_task(pt, ((PathTask*)where[i])->children);
				return;
			case OV_AFTER:
				break;
			case OV_SPAN:
				assert(!"insert parents first");
				break;
			default:
				assert(!"invalid overlap type");
		}
	}
	if (where.size() != 0)
		assert(where[where.size()-1]->end() <= pt->start());
	where.push_back(pt);
}

void Path::insert_event(PathEvent *pe, std::vector<PathEvent *> &where) {
	assert(pe->type() != PEV_TASK);
	for (unsigned int i=0; i<where.size(); i++) {
		if (pe->start() < where[i]->start()) {      // before where[i]
			where.insert(where.begin()+i, pe);
			return;
		}
		if (where[i]->type() != PEV_TASK) continue;
		if (pe->start() <= where[i]->end()) {       // inside where[i]
			insert_event(pe, ((PathTask*)where[i])->children);
			return;
		}
	}
	where.push_back(pe);
}

void Path::print(FILE *fp) const {
#ifdef PRINT_DOT
	fprintf(fp, "digraph foo {\n");
	for (std::map<int,PathEventList>::const_iterator list=children_map.begin(); list!=children_map.end(); list++) {
		for (unsigned int i=0; i<list->second.size(); i++)
			list->second[i]->print(fp, 0);
	}
	fprintf(fp, "}\n");
#else
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, 0);
#endif
}

static int count_messages(const PathEventList &list) {
	int ret = 0;
	for (unsigned int i=0; i<list.size(); i++)
		switch (list[i]->type()) {
			case PEV_MESSAGE_SEND:
			case PEV_MESSAGE_RECV:
				ret++;
				break;
			case PEV_TASK:
				ret += count_messages(((PathTask*)list[i])->children);
				break;
			case PEV_NOTICE: break;
			default:
				fprintf(stderr, "Invalid event type in count_messages: %d\n", list[i]->type());
				abort();
		}
	return ret;
}

static PathEvent *first_message(const PathEventList &list) {
	PathEvent *ret;
	for (unsigned int i=0; i<list.size(); i++)
		switch (list[i]->type()) {
			case PEV_MESSAGE_SEND:
			case PEV_MESSAGE_RECV:
				return list[i];
			case PEV_TASK:
				if ((ret = first_message(((PathTask*)list[i])->children)) != NULL)
					return ret;
				break;
			case PEV_NOTICE: break;
			default:
				fprintf(stderr, "Invalid event type in first_message: %d\n", list[i]->type());
				abort();
		}
	return NULL;
}

static PathMessageRecv *set_message_predecessors(PathEventList &list, PathMessageRecv *pred) {
	for (unsigned int i=0; i<list.size(); i++)
		switch (list[i]->type()) {
			case PEV_MESSAGE_SEND:
				((PathMessageSend*)list[i])->pred = pred;
				break;
			case PEV_MESSAGE_RECV:
				pred = ((PathMessageRecv*)list[i]);
				break;
			case PEV_TASK:
				pred = set_message_predecessors(((PathTask*)list[i])->children, pred);
				break;
			case PEV_NOTICE:
				break;
			default:
				fprintf(stderr, "Invalid event type in count_messages: %d\n", list[i]->type());
				abort();
		}
	return pred;
}

static void check_order(const PathEventList &list) {
	unsigned int i;
	for (i=0; i<list.size(); i++) {
		assert(list[i]->start() <= list[i]->end());
		if (i < list.size()-1) {
			if (list[i]->end() > list[i+1]->start()) {
				fprintf(stderr, "Events %d/%d out of order:\n", i, i+1);
				list[i]->print(stderr, 1);
				list[i+1]->print(stderr, 1);
			}
			assert(list[i]->end() <= list[i+1]->start());
		}
		if (list[i]->type() == PEV_TASK) {
			const PathTask *t = (const PathTask*)list[i];
			if (t->children.size() > 0) {
				assert(t->children[0]->start() >= t->start());
				assert(t->children[t->children.size()-1]->end() <= t->end());
				check_order(t->children);
			}
		}
	}
}

void Path::done_inserting(void) {
	std::map<int,PathEventList>::iterator thread;

	assert(children_map.size() > 0);

	//fprintf(stderr, "Checking Path %d\n", path_id);
	for (thread=children_map.begin(); thread!=children_map.end(); thread++) {
		//fprintf(stderr, "  Checking Thread %d\n", thread->first);
		//for (unsigned int i=0; i<thread->second.size(); i++)
			//thread->second[i]->print(stderr, 2);
		check_order(thread->second);
	}

	// Can the DAG be connected?  Either all threads must have message
	// events, or there must be exactly one thread.
	if (children_map.size() > 1) {
		for (thread=children_map.begin(); thread!=children_map.end(); thread++) {
			int msg_count = count_messages(thread->second);
			assert(msg_count != 0);
		}

		// set all PathMessageSend.pred fields
		for (thread=children_map.begin(); thread!=children_map.end(); thread++)
			set_message_predecessors(thread->second, NULL);

		// find the root -- the only send without a predecessor
		PathMessageSend *root = NULL;
		for (thread=children_map.begin(); thread!=children_map.end(); thread++) {
			PathEvent *ev = first_message(thread->second);
			if (ev->type() == PEV_MESSAGE_SEND) {
				if (root != NULL) {
					fprintf(stderr, "Path %d has multiple roots!\n", path_id);
					ev->print(stderr, 1);
					//abort();
				}
				else {
					root = (PathMessageSend*)ev;
					assert(root->pred == NULL);
				}
			}
		}
		assert(root != NULL);
		//PathThread *root_thread = threads[((PathMessageSend*)ev)->thread_send];

		// !! make sure the DAG is connected -- do we touch all events?
		//
		// Hmmm, I think this is implied by finding a unique root.

#ifndef PRINT_DOT
		children.swap(children_map[root->thread_send]);
	}
	else
		children.swap(children_map.begin()->second);
	children_map.clear();
#else
	}
#endif

#if 0
kludge:
	children.swap(children_map.begin()->second);
	children_map.clear();
#endif
	if (children.size() == 0) return;
	ts_start = children[0]->start();
	ts_end = children[children.size()-1]->end();
	tally(&children, true);
}

void Path::tally(const PathEventList *list, bool toplevel) {
	for (unsigned int i=0; i<list->size(); i++) {
		const PathEvent *ev = (*list)[i];
		switch (ev->type()) {
			case PEV_TASK:
				// Clocks are still running for the parent, so don't add the
				// children, too -- that's double-billing
				if (toplevel) {
					utime += ((PathTask*)ev)->utime;
					stime += ((PathTask*)ev)->stime;
					major_fault += ((PathTask*)ev)->major_fault;
					minor_fault += ((PathTask*)ev)->minor_fault;
					vol_cs += ((PathTask*)ev)->vol_cs;
					invol_cs += ((PathTask*)ev)->invol_cs;
				}
				tally(&((PathTask*)ev)->children, false);
				break;
			case PEV_NOTICE:
				break;
			case PEV_MESSAGE_SEND:
				size += ((PathMessageSend*)ev)->size;
				break;
			case PEV_MESSAGE_RECV:
				break;
			default:
				assert(!"invalid PathEventType");
		}
	}
}
