/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <stdarg.h>
#include "common.h"
#include "path.h"

#define PRINT_EXP_GROUP_THREADS
// !! should maybe use existing comparison methods and allow threads with
// different task/notices names or message sources/destinations to be
// considered the same
// !! having this feature on breaks send destination and recv source.  If
// I send(t_7) and t_7 is the same as t_3, I ought to be saying send(t_3).

std::map<int, PathThread*> threads;
std::map<int, std::vector<PathThread *> > thread_pools;
const char *path_type_name[] = { "task", "notice", "send", "recv" };

static void print_exp_children(FILE *fp, unsigned int depth, const PathEventList &list);
static const char *indent(unsigned int depth);

timeval ts_to_tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}

PathTask::PathTask(const MYSQL_ROW &row) {
	path_id = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	level = atoi(row[2]);
	name = strdup(row[3]);
	ts = ts_to_tv(strtoll(row[4], NULL, 10));
	ts_end = ts_to_tv(strtoll(row[5], NULL, 10));
	tdiff = atoi(row[6]);
	utime = atoi(row[7]);
	stime = atoi(row[8]);
	major_fault = atoi(row[9]);
	minor_fault = atoi(row[10]);
	vol_cs = atoi(row[11]);
	invol_cs = atoi(row[12]);
	thread_id = atoi(row[13]);
}

int PathTask::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_TASK) return -PCMP_NONE;
	if (_other->type() > PEV_TASK) return PCMP_NONE;
	const PathTask *other = dynamic_cast<const PathTask*>(_other);
	if (children.size() < other->children.size()) return -PCMP_NONE;
	if (children.size() > other->children.size()) return PCMP_NONE;
	PathEventList::const_iterator evp, oevp;
	for (evp = children.begin(), oevp = other->children.begin();
			evp != children.end() && oevp != other->children.end();
			evp++, oevp++) {
		int res = (*evp)->compare(*oevp);
		if (res != PCMP_EXACT) return res;
	}
	int res = strcmp(name, other->name);
	if (res < 0) return -PCMP_NAMES;
	if (res > 0) return PCMP_NAMES;
	return PCMP_EXACT;
}

std::string PathTask::to_string(void) const {
	return std::string("Task(\"") + name + "\")";
}

void PathTask::print_dot(FILE *fp) const {
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print_dot(fp);
}

void PathTask::print(FILE *fp, unsigned int depth) const {
	bool empty = children.size() == 0;

	fprintf(fp, "%*s<task name=\"%s\" start=\"%ld.%06ld\" end=\"%ld.%06ld\" level=\"%d\"%s",
		depth*2, "", name, ts.tv_sec, ts.tv_usec,
		ts_end.tv_sec, ts_end.tv_usec, level, empty ? " />\n" : ">\n");
	if (!empty) {
		for (unsigned int i=0; i<children.size(); i++)
			children[i]->print(fp, depth+1);
		fprintf(fp, "%*s</task>\n", depth*2, "");
	}
}

void PathTask::print_exp(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%stask(\"%s\")", indent(depth), name);
	if (children.size() != 0) {
		fprintf(fp, " {\n");
		print_exp_children(fp, depth+1, children);
		fprintf(fp, "%s}\n", indent(depth));
	}
	else fputs(";\n", fp);
}

int PathTask::cmp(const PathEvent *other) const {
	int ret;

	if ((ret = PEV_TASK - other->type()) != 0) return ret;
	if ((ret = strcmp(name, dynamic_cast<const PathTask*>(other)->name)) != 0) return ret;
	if ((ret = children.size() - dynamic_cast<const PathTask*>(other)->children.size()) != 0) return ret;
	for (unsigned int i=0; i<children.size(); i++)
		if ((ret = children[i]->cmp(dynamic_cast<const PathTask*>(other)->children[i])) != 0) return ret;

	return 0;
}

PathTask::~PathTask(void) {
	free(name);
	for (unsigned int i=0; i<children.size(); i++) delete children[i];
}

PathNotice::PathNotice(const MYSQL_ROW &row) {
	path_id = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	level = atoi(row[2]);
	name = strdup(row[3]);
	ts = ts_to_tv(strtoll(row[4], NULL, 10));
	thread_id = atoi(row[5]);
}

int PathNotice::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_NOTICE) return -PCMP_NONE;
	if (_other->type() > PEV_NOTICE) return PCMP_NONE;
	const PathNotice *other = dynamic_cast<const PathNotice*>(_other);
	int res = strcmp(name, other->name);
	if (res < 0) return -PCMP_NAMES;
	if (res > 0) return PCMP_NAMES;
	return PCMP_EXACT;
}

std::string PathNotice::to_string(void) const {
	return std::string("Notice(\"") + name + "\")";
}

void PathNotice::print_dot(FILE *fp) const { }

void PathNotice::print(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" ts=\"%ld.%06ld\" level=\"%d\" />\n", depth*2, "",
		name, ts.tv_sec, ts.tv_usec, level);
}

void PathNotice::print_exp(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%snotice(\"%s\");\n", indent(depth), name);
}

int PathNotice::cmp(const PathEvent *other) const {
	int ret;
	if ((ret = PEV_NOTICE - other->type()) != 0) return ret;
	return strcmp(name, dynamic_cast<const PathNotice*>(other)->name);
}

PathMessageSend::PathMessageSend(const MYSQL_ROW &row) : dest(NULL), pred(NULL) {
	path_id = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	level = atoi(row[2]);
	// msgid is row[3]
	ts = ts_to_tv(strtoll(row[4], NULL, 10));
	// ts_recv is row[5]
	size = atoi(row[6]);
	thread_id = atoi(row[7]);
	// thread_recv is row[8]
}

int PathMessageSend::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_MESSAGE_SEND) return -PCMP_NONE;
	if (_other->type() > PEV_MESSAGE_SEND) return PCMP_NONE;
	const PathMessageSend *other = dynamic_cast<const PathMessageSend*>(_other);
	// !! this will have to change if we allow thread-reordering
	// !! really should check to make sure the right thread receives the msg
	// These checks are all messed up because 'thread_id' refers to the
	// absolute thread_id (which can differ), not the position in our path's
	// 'children' list (which should, maybe, be the same).
	//assert(thread_id == other->thread_id);
	//if (recv->thread_id < other->recv->thread_id) return -PCMP_NONE;
	//if (recv->thread_id > other->recv->thread_id) return PCMP_NONE;

	if (size < other->size) return -PCMP_NAMES;
	if (size > other->size) return PCMP_NAMES;

	return PCMP_EXACT;
}

std::string PathMessageSend::to_string(void) const {
	char buf[256];
	if (recv)
		snprintf(buf, sizeof(buf), "Send(%d->%d, %d bytes)",
			thread_id, recv->thread_id, size);
	else
		snprintf(buf, sizeof(buf), "Send(%d->(null), %d bytes)",
			thread_id, size);
	return std::string(buf);
}

void PathMessageSend::print_dot(FILE *fp) const {
	if (!pred) {
		PathThread *t = threads[thread_id];
		fprintf(fp, "s%x [label = \"%d:%s/%s/%d\"];\n",
			(int)this, thread_id, t->host.c_str(), t->prog.c_str(), t->tid);
	}
	//fprintf(fp, "r%x -> s%x;\n", (int)pred, (int)this);
}

void PathMessageSend::print(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%*s<message_send size=\"%d\" send=\"%d\" recv=\"%d\" ts=\"%ld.%06ld\" addr=\"%p\" level=\"%d\" />\n",
		depth*2, "", size, thread_id, recv?recv->thread_id:0, ts.tv_sec, ts.tv_usec, this, level);
}

void PathMessageSend::print_exp(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%ssend(t_%d);\n", indent(depth), recv?recv->thread_id:0);
}

int PathMessageSend::cmp(const PathEvent *other) const {
	int ret;
	const PathMessageSend *s_other = dynamic_cast<const PathMessageSend*>(other);
	if ((ret = PEV_MESSAGE_SEND - other->type()) != 0) return ret;
	if ((ret = size - s_other->size) != 0) return ret;
	if (!recv && s_other->recv) return -1;
	if (recv && !s_other->recv) return 1;
	if (!recv) return 0;  // impies !s_other->recv
	return recv->thread_id - s_other->recv->thread_id;
}

PathMessageRecv::PathMessageRecv(const MYSQL_ROW &row) : send(NULL) {
	path_id = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	level = atoi(row[2]);
	// msgid is row[3]
	// ts_send is row[4]
	ts = ts_to_tv(strtoll(row[5], NULL, 10));
	// size is row[6]
	// thread_send is row[7]
	thread_id = atoi(row[8]);
}

int PathMessageRecv::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_MESSAGE_RECV) return -PCMP_NONE;
	if (_other->type() > PEV_MESSAGE_RECV) return PCMP_NONE;
	//const PathMessageRecv *other = dynamic_cast<const PathMessageRecv*>(_other);
	// !! this will have to change if we allow thread-reordering
	// !! should check to make sure the right thread sends the msg
	// Messed up, same as PathMessageSend::compare.
	// assert(thread_id == other->thread_id);
	//if (send->thread_id < other->send->thread_id) return -PCMP_NONE;
	//if (send->thread_id > other->send->thread_id) return PCMP_NONE;

	return PCMP_EXACT;
}

std::string PathMessageRecv::to_string(void) const {
	char buf[256];
	snprintf(buf, sizeof(buf), "Recv(%d->%d, %d bytes)",
		send->thread_id, thread_id, send->size);
	return std::string(buf);
}

void PathMessageRecv::print_dot(FILE *fp) const {
	PathThread *t = threads[thread_id];
	fprintf(fp, "r%x [label = \"%d:%s/%s/%d\"];\n",
		(int)this, thread_id, t->host.c_str(), t->prog.c_str(), t->tid);
	if (send->pred)
		fprintf(fp, "r%x -> r%x;\n", (int)send->pred, (int)this);
	else
		fprintf(fp, "s%x -> r%x;\n", (int)send, (int)this);
}

void PathMessageRecv::print(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%*s<message_recv send=\"%d\" recv=\"%d\" ts=\"%ld.%06ld\" send=\"%p\" level=\"%d\" />\n",
		depth*2, "", send->thread_id, thread_id, ts.tv_sec, ts.tv_usec, send, level);
}

void PathMessageRecv::print_exp(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%srecv(t_%d);\n", indent(depth), send->thread_id);
}

int PathMessageRecv::cmp(const PathEvent *other) const {
	int ret;
	if ((ret = PEV_MESSAGE_RECV - other->type()) != 0) return ret;
	if ((ret = send->size - dynamic_cast<const PathMessageRecv*>(other)->send->size) != 0) return ret;
	return send->thread_id - dynamic_cast<const PathMessageRecv*>(other)->send->thread_id;
}

PathThread::PathThread(const MYSQL_ROW &row) {
	thread_id = atoi(row[0]);
	host = row[1];
	prog = row[2];
	pid = atoi(row[3]);
	tid = atoi(row[4]);
	ppid = atoi(row[5]);
	uid = atoi(row[6]);
	start = ts_to_tv(strtoll(row[7], NULL, 10));
	tz = atoi(row[8]);
}

void PathThread::print(FILE *fp, unsigned int depth) const {
	fprintf(fp, "%*s<thread id=\"%d\" host=\"%s\" prog=\"%s\" pid=\"%d\" tid=\"%d\" ppid=\"%d\" "
		"uid=\"%d\" start=\"%ld.%06ld\" tz=\"%s%02d%02d\" />\n", depth*2, "", thread_id, host.c_str(),
		prog.c_str(), pid, tid, ppid, uid, start.tv_sec, start.tv_usec,
		(tz < 0 ? "+" : "-"), abs(tz)/60, abs(tz)%60);
}

Path::Path(void) {
	init();
}

Path::Path(MYSQL *mysql, const char *base, int _path_id) {
	init();
	read(mysql, base, _path_id);
}

void Path::init(void) {
	utime = stime = 0;
	major_fault = minor_fault = 0;
	vol_cs = invol_cs = 0;
	size = messages = depth = hosts = latency = 0;
	root_thread = -1;
	ts_start.tv_sec = ts_start.tv_usec = 0;
	ts_end.tv_sec = ts_end.tv_usec = 0;
}

Path::~Path(void) {
	std::map<int,PathEventList>::const_iterator thread;
	for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++)
		for (unsigned int i=0; i<thread->second.size(); i++)
			delete thread->second[i];
}

void Path::read(MYSQL *mysql, const char *base, int _path_id) {
	path_id = _path_id;

	run_sqlf(mysql, "SELECT * FROM %s_tasks WHERE pathid=%d ORDER BY start",
		base, path_id);
	MYSQL_RES *res = mysql_use_result(mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == path_id);
		PathTask *pt = new PathTask(row);
		insert(pt);
	}
	mysql_free_result(res);

	run_sqlf(mysql, "SELECT * FROM %s_notices WHERE pathid=%d", base, path_id);
	res = mysql_use_result(mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == path_id);
		PathNotice *pn = new PathNotice(row);
		//pn->print(stderr);
		insert(pn);
	}
	mysql_free_result(res);

	run_sqlf(mysql, "SELECT * FROM %s_messages WHERE pathid=%d", base, path_id);
	res = mysql_use_result(mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == path_id);
		PathMessage *pm = new PathMessage(row);
		insert(pm);
	}
	mysql_free_result(res);

	done_inserting();
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

// returns negative numbers for us < them
// returns PCMP_EXACT if shape + hostnames match
// returns PCMP_NAMES if shapes match, task/notice names differ
//   -> sign indicates order
int Path::compare(const Path &other) const {
	if (thread_pools.size() < other.thread_pools.size()) return -PCMP_NONE;
	if (thread_pools.size() > other.thread_pools.size()) return PCMP_NONE;
	std::map<int,PathEventList>::const_iterator cp, ocp;
	// !! we may have to try threads in all orders, not sure
	// it would be O(n^2), often less
	for (cp = thread_pools.begin(),ocp = other.thread_pools.begin();
			cp != thread_pools.end() && ocp != other.thread_pools.end();
			cp++, ocp++) {
		if (cp->second.size() < ocp->second.size()) return -PCMP_NONE;
		if (cp->second.size() > ocp->second.size()) return PCMP_NONE;

		PathEventList::const_iterator evp, oevp;
		for (evp = cp->second.begin(), oevp = ocp->second.begin();
				evp != cp->second.end() && oevp != ocp->second.end();
				evp++, oevp++) {
			int res = (*evp)->compare(*oevp);
			if (res != PCMP_EXACT) return res;
		}
	}
	return PCMP_EXACT;
}

void Path::insert_task(PathTask *pt, std::vector<PathEvent *> &where) {
	if (where.size() != 0) {
		int idx = where.size() - 1;
		switch (tvcmp(where[idx], pt)) {
			case OV_START:
				assert(!"New PathTask overlaps start of existing");
			case OV_END:
				assert(!"New PathTask overlaps end of existing");
			case OV_BEFORE:
				where.insert(where.begin()+idx, pt);
				return;
			case OV_WITHIN:
				assert(where[idx]->type() == PEV_TASK);
				insert_task(pt, dynamic_cast<PathTask*>(where[idx])->children);
				return;
			case OV_AFTER:
				break;
			case OV_SPAN:
				assert(!"insert parents first");
				break;
			default:
				assert(!"invalid overlap type");
		}

		assert(where[idx]->end() <= pt->start());
	}
	where.push_back(pt);
}

void Path::insert_event(PathEvent *pe, std::vector<PathEvent *> &where) {
	assert(pe->type() != PEV_TASK);
	const timeval &start = pe->start();
	int low=0, high=where.size()-1;
	bool duplicate_timestamp_warning = false;
	while (low <= high) {
		unsigned int mid = (low+high)/2;
		const timeval &test_start = where[mid]->start();
		if (start < test_start) high = mid-1;
		else if (start > test_start) low = mid+1;
		else {
			duplicate_timestamp_warning = true;
			low = mid+1;   // put the new timestamp after the old one
		}
	}
	if (duplicate_timestamp_warning)
		fprintf(stderr, "Warning: two events with timestamp %ld.%06ld\n", start.tv_sec, start.tv_usec);
	assert(low == high+1);

	if (high >= 0 && where[high]->type() == PEV_TASK && start <= where[high]->end())
		insert_event(pe, dynamic_cast<PathTask*>(where[high])->children);
	else
		where.insert(where.begin()+low, pe);
}

void Path::print_dot(FILE *fp) const {
	fprintf(fp, "digraph foo {\n");
	for (std::map<int,PathEventList>::const_iterator list=thread_pools.begin(); list!=thread_pools.end(); list++) {
		for (unsigned int i=0; i<list->second.size(); i++)
			list->second[i]->print_dot(fp);
	}
	fprintf(fp, "}\n");
}

void Path::print(FILE *fp) const {
	for (std::map<int,PathEventList>::const_iterator list=thread_pools.begin(); list!=thread_pools.end(); list++) {
		fprintf(fp, "<thread id=\"%d\"%s>\n",
			list->first, list->first == root_thread ? " root=\"true\"" : "");
		for (unsigned int i=0; i<list->second.size(); i++)
			list->second[i]->print(fp, 1);
		fprintf(fp, "</thread>\n");
	}
}

#ifdef PRINT_EXP_GROUP_THREADS
struct lt_evlist {
	bool operator()(const PathEventList *l1, const PathEventList *l2) const {
		int ret;
		if ((ret = l1->size() - l2->size()) != 0) return ret < 0;
		for (unsigned int i=0; i<l1->size(); i++)
			if ((ret = (*l1)[i]->cmp((*l2)[i])) != 0) return ret < 0;
		return 0;
	}
};
#endif
void Path::print_exp(FILE *fp) const {
	fprintf(fp, "\tlimit(THREADS, {=%d});\n", thread_pools.size());
	for (std::map<int,PathEventList>::const_iterator list=thread_pools.begin(); list!=thread_pools.end(); list++) {
		if (list->first == root_thread) {
			fprintf(fp, "\tthread t_%d(*, 1) {   // root\n", list->first);
			print_exp_children(fp, 2, list->second);
			fprintf(fp, "\t}\n");
			break;
		}
	}
#ifdef PRINT_EXP_GROUP_THREADS
	std::map<const PathEventList*, int, lt_evlist> uniq_threads;
	std::map<const PathEventList*, int> thread_id_map;
	for (std::map<int,PathEventList>::const_iterator list=thread_pools.begin(); list!=thread_pools.end(); list++) {
		if (list->first == root_thread) continue;
		if (++uniq_threads[&list->second] == 1) thread_id_map[&list->second] = list->first;
	}
	for (std::map<const PathEventList*, int, lt_evlist>::const_iterator thr=uniq_threads.begin(); thr!=uniq_threads.end(); thr++) {
		fprintf(fp, "\tthread t_%d(*, %d) {\n", thread_id_map[thr->first], thr->second);
		print_exp_children(fp, 2, *thr->first);
		fprintf(fp, "\t}\n");
	}
#else
	for (std::map<int,PathEventList>::const_iterator list=thread_pools.begin(); list!=thread_pools.end(); list++) {
		if (list->first == root_thread) continue;
		fprintf(fp, "\tthread t_%d(*, 1) {\n", list->first);
		print_exp_children(fp, 2, list->second);
		fprintf(fp, "\t}\n");
	}
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
				ret += count_messages(dynamic_cast<PathTask*>(list[i])->children);
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
				if ((ret = first_message(dynamic_cast<PathTask*>(list[i])->children)) != NULL)
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
				dynamic_cast<PathMessageSend*>(list[i])->pred = pred;
				break;
			case PEV_MESSAGE_RECV:
				pred = dynamic_cast<PathMessageRecv*>(list[i]);
				break;
			case PEV_TASK:
				pred = set_message_predecessors(dynamic_cast<PathTask*>(list[i])->children, pred);
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
		if (i < list.size()-1) assert(list[i]->end() <= list[i+1]->start());
		if (list[i]->type() == PEV_TASK) {
			const PathTask *t = dynamic_cast<const PathTask*>(list[i]);
			if (t->children.size() > 0) {
				assert(t->children[0]->start() >= t->start());
				assert(t->children[t->children.size()-1]->end() <= t->end());
				check_order(t->children);
			}
		}
	}
}

static unsigned int recv_message_depth(const PathMessageRecv *pmr);
static unsigned int send_message_depth(const PathMessageSend *pms) {
	const PathMessageRecv *pred = pms->pred;
	return pred ? recv_message_depth(pred) : 1;
}

static unsigned int recv_message_depth(const PathMessageRecv *pmr) {
	const PathMessageSend *pred = pmr->send;
	return 1+send_message_depth(pred);
}

// !! This is O(n^2), because each call traces its way all the way back to
// the root.  Caching using std::map was more expensive.  Letting each PMR
// and PMS remember its own depth would be cheap.
static unsigned int max_message_depth(const PathEventList &list) {
	unsigned int max = 1;
	for (unsigned int i=0; i<list.size(); i++) {
		unsigned int depth = 0;
		switch (list[i]->type()) {
			case PEV_TASK:
				depth = max_message_depth(dynamic_cast<PathTask*>(list[i])->children);
				break;
			case PEV_MESSAGE_RECV:
				depth = recv_message_depth(dynamic_cast<PathMessageRecv*>(list[i]));
				break;
			default: ;
		}
		if (depth > max) max = depth;
	}
	return max;
}

void Path::done_inserting(void) {
	std::map<int,PathEventList>::iterator thread;

	if (thread_pools.size() == 0) {
		fprintf(stderr, "No threads -- empty path ??\n");
		return;
	}

	//fprintf(stderr, "Checking Path %d\n", path_id);
	for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++) {
		//fprintf(stderr, "  Checking Thread %d\n", thread->first);
		//for (unsigned int i=0; i<thread->second.size(); i++)
			//thread->second[i]->print(stderr, 2);
		check_order(thread->second);
	}

	// Can the DAG be connected?  Either all threads must have message
	// events, or there must be exactly one thread.
	if (thread_pools.size() > 1) {
		for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++) {
			int msg_count = count_messages(thread->second);
			if (msg_count == 0) {
				fprintf(stderr, "Malformed path -- unconnected causality DAG: thread %d has no messages\n", thread->first);
				return;
			}
		}

		// find the root -- the only send without a predecessor
		PathMessageSend *root = NULL;
		for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++) {
			PathEvent *ev = first_message(thread->second);
			if (ev->type() == PEV_MESSAGE_SEND) {
				if (root != NULL) {
					fprintf(stderr, "Path %d has multiple roots!\n", path_id);
					ev->print(stderr, 1);
					return;
				}
				else {
					root = dynamic_cast<PathMessageSend*>(ev);
					assert(root->pred == NULL);
				}
			}
		}
		assert(root != NULL);
		root_thread = threads[root->thread_id]->pool;
		//PathThread *root_thread = threads[dynamic_cast<PathMessageSend*>(ev)->thread_id];

		// !! make sure the DAG is connected -- do we touch all events?
		//
		// Hmmm, I think this is implied by finding a unique root.

	}
	else
		root_thread = thread_pools.begin()->first;

	// set all PathMessageSend.pred fields
	for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++)
		set_message_predecessors(thread->second, NULL);

	assert(!thread_pools.begin()->second.empty());  // root thread not empty
	// !! root may not be the last to end
	// but anything else assumes synchronized clocks
	ts_start = thread_pools[root_thread][0]->start();
	ts_end = thread_pools[root_thread][thread_pools[root_thread].size()-1]->end();

	for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++)
		tally(thread->second, true);

	depth = 1;
	std::set<std::string> host_set;
	for (thread=thread_pools.begin(); thread!=thread_pools.end(); thread++) {
		int this_depth = max_message_depth(thread->second);
		if (this_depth > depth) depth = this_depth;

		assert(!thread->second.empty());
		host_set.insert(threads[get_thread_id(thread->second)]->host);
	}
	hosts = host_set.size();
}

void Path::tally(const PathEventList &list, bool toplevel) {
	for (unsigned int i=0; i<list.size(); i++) {
		const PathEvent *ev = list[i];
		switch (ev->type()) {
			case PEV_TASK:
				// Clocks are still running for the parent, so don't add the
				// children, too -- that's double-billing
				if (toplevel) {
					utime += dynamic_cast<const PathTask*>(ev)->utime;
					stime += dynamic_cast<const PathTask*>(ev)->stime;
					major_fault += dynamic_cast<const PathTask*>(ev)->major_fault;
					minor_fault += dynamic_cast<const PathTask*>(ev)->minor_fault;
					vol_cs += dynamic_cast<const PathTask*>(ev)->vol_cs;
					invol_cs += dynamic_cast<const PathTask*>(ev)->invol_cs;
				}
				tally(dynamic_cast<const PathTask*>(ev)->children, false);
				break;
			case PEV_NOTICE:
				break;
			case PEV_MESSAGE_SEND: {
					const PathMessageSend *pms = dynamic_cast<const PathMessageSend*>(ev);
					size += pms->size;
					if (pms->recv) latency += pms->recv->ts - pms->ts;
				}
				messages++;
				break;
			case PEV_MESSAGE_RECV:
				break;
			default:
				assert(!"invalid PathEventType");
		}
	}
}

void run_sql(MYSQL *mysql, const char *query) {
	//fprintf(stderr, "SQL(\"%s\")\n", query);
	if (mysql_query(mysql, query) != 0) {
		fprintf(stderr, "Database error:\n");
		fprintf(stderr, "  QUERY: \"%s\"\n", query);
		fprintf(stderr, "  MySQL error: \"%s\"\n", mysql_error(mysql));
		exit(1);
	}
}

void run_sqlf(MYSQL *mysql, const char *fmt, ...) {
	char query[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(query, fmt, arg);
	va_end(arg);
	run_sql(mysql, query);
}

void get_path_ids(MYSQL *mysql, const char *table_base, std::set<int> *pathids) {
	MYSQL_RES *res;
	MYSQL_ROW row;

	fprintf(stderr, "Reading pathids...");
	run_sqlf(mysql, "SELECT distinct pathid from %s_tasks", table_base);
	res = mysql_use_result(mysql);
	while ((row = mysql_fetch_row(res)) != NULL)
		pathids->insert(atoi(row[0]));
	mysql_free_result(res);
	run_sqlf(mysql, "SELECT distinct pathid from %s_notices", table_base);
	res = mysql_use_result(mysql);
	while ((row = mysql_fetch_row(res)) != NULL)
		pathids->insert(atoi(row[0]));
	mysql_free_result(res);
	run_sqlf(mysql, "SELECT distinct pathid from %s_messages", table_base);
	res = mysql_use_result(mysql);
	while ((row = mysql_fetch_row(res)) != NULL)
		pathids->insert(atoi(row[0]));
	mysql_free_result(res);

	fprintf(stderr, " done: %d found.\n", pathids->size());
}

void get_threads(MYSQL *mysql, const char *table_base, std::map<int, PathThread*> *threads) {
	typedef std::pair<std::string, int> StringInt;
	typedef std::map<StringInt, int> ThreadPoolMap;
	ThreadPoolMap thread_pool_map;
	int next_thread_pool_id = 1;

	thread_pools.clear();
	fprintf(stderr, "Reading threads...");
	run_sqlf(mysql, "SELECT * from %s_threads", table_base);
	MYSQL_RES *res = mysql_use_result(mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		PathThread *thr = new PathThread(row);
		(*threads)[atoi(row[0])] = thr;
		StringInt key(thr->host, thr->pid);
		ThreadPoolMap::iterator p = thread_pool_map.find(key);
		if (p == thread_pool_map.end())
			thread_pool_map[key] = next_thread_pool_id++;

		thr->pool = thread_pool_map[key];
		thread_pools[thr->pool].push_back(thr);
	}
	mysql_free_result(res);
	fprintf(stderr, " done: %d found.\n", threads->size());
}

static void print_exp_children(FILE *fp, unsigned int depth, const PathEventList &list) {
	for (unsigned int i=0; i<list.size(); i++) {
		// !! look for repeated sequences of larger sizes than one
		unsigned int ahead = i+1;
		for ( ; ahead<list.size() && *list[i] == *list[ahead]; ahead++)  ;
		if (ahead - i > 1) {
			fprintf(fp, "%srepeat between %d and %d {\n", indent(depth), ahead-i, ahead-i);
			list[i]->print_exp(fp, depth+1);
			fprintf(fp, "%s}\n", indent(depth));
			i = ahead-1;
		}
		else
			list[i]->print_exp(fp, depth);
	}
}

static const char *indent(unsigned int depth) {
	static char ret[20];
	assert(depth < sizeof(ret)-1);
	memset(ret, '\t', depth);
	ret[depth] = '\0';
	return ret;
}
