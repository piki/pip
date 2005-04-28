#include <assert.h>
#include <stdarg.h>
#include "common.h"
#include "path.h"

std::map<int, PathThread*> threads;

inline static timeval tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}

PathTask::PathTask(const MYSQL_ROW &row) {
	// pathid = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	// level = atoi(row[2]);
	name = strdup(row[3]);
	ts_start = tv(strtoll(row[4], NULL, 10));
	ts_end = tv(strtoll(row[5], NULL, 10));
	tdiff = atoi(row[6]);
	utime = atoi(row[7]);
	stime = atoi(row[8]);
	major_fault = atoi(row[9]);
	minor_fault = atoi(row[10]);
	vol_cs = atoi(row[11]);
	invol_cs = atoi(row[12]);
	thread_start = atoi(row[13]);
	thread_end = atoi(row[14]);
}

int PathTask::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_TASK) return -PCMP_NONE;
	if (_other->type() > PEV_TASK) return PCMP_NONE;
	const PathTask *other = (PathTask*)_other;
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

void PathTask::print(FILE *fp, int depth) const {
	bool empty = children.size() == 0;

	fprintf(fp, "%*s<task name=\"%s\" start=\"%ld.%06ld\" end=\"%ld.%06ld\"%s",
		depth*2, "", name, ts_start.tv_sec, ts_start.tv_usec,
		ts_end.tv_sec, ts_end.tv_usec, empty ? " />\n" : ">\n");
	if (!empty) {
		for (unsigned int i=0; i<children.size(); i++)
			children[i]->print(fp, depth+1);
		fprintf(fp, "%*s</task>\n", depth*2, "");
	}
}

PathTask::~PathTask(void) {
	free(name);
	for (unsigned int i=0; i<children.size(); i++) delete children[i];
}

PathNotice::PathNotice(const MYSQL_ROW &row) {
	// pathid = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	// level = atoi(row[2]);
	name = strdup(row[3]);
	ts = tv(strtoll(row[4], NULL, 10));
	thread_id = atoi(row[5]);
}

int PathNotice::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_NOTICE) return -PCMP_NONE;
	if (_other->type() > PEV_NOTICE) return PCMP_NONE;
	const PathNotice *other = (PathNotice*)_other;
	int res = strcmp(name, other->name);
	if (res < 0) return -PCMP_NAMES;
	if (res > 0) return PCMP_NAMES;
	return PCMP_EXACT;
}

std::string PathNotice::to_string(void) const {
	return std::string("Notice(\"") + name + "\")";
}

void PathNotice::print_dot(FILE *fp) const { }

void PathNotice::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" ts=\"%ld.%06ld\" />\n", depth*2, "",
		name, ts.tv_sec, ts.tv_usec);
}

PathMessageSend::PathMessageSend(const MYSQL_ROW &row) : dest(NULL), pred(NULL) {
	// pathid = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	// level = atoi(row[2]);
	// msgid is row[3]
	ts_send = tv(strtoll(row[4], NULL, 10));
	// ts_recv is row[5]
	size = atoi(row[6]);
	thread_send = atoi(row[7]);
	// thread_recv is row[8]
}

int PathMessageSend::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_MESSAGE_SEND) return -PCMP_NONE;
	if (_other->type() > PEV_MESSAGE_SEND) return PCMP_NONE;
	const PathMessageSend *other = (PathMessageSend*)_other;
	// !! this will have to change if we allow thread-reordering
	// !! really should check to make sure the right thread receives the msg
	// These checks are all messed up because 'thread_send' refers to the
	// absolute thread_id (which can differ), not the position in our path's
	// 'children' list (which should, maybe, be the same).
	//assert(thread_send == other->thread_send);
	//if (recv->thread_recv < other->recv->thread_recv) return -PCMP_NONE;
	//if (recv->thread_recv > other->recv->thread_recv) return PCMP_NONE;

	if (size < other->size) return -PCMP_NAMES;
	if (size > other->size) return PCMP_NAMES;

	return PCMP_EXACT;
}

std::string PathMessageSend::to_string(void) const {
	char buf[256];
	snprintf(buf, sizeof(buf), "Send(%d->%d, %d bytes)",
		thread_send, recv->thread_recv, size);
	return std::string(buf);
}

void PathMessageSend::print_dot(FILE *fp) const {
	if (!pred) {
		PathThread *t = threads[thread_send];
		fprintf(fp, "s%x [label = \"%d:%s/%s/%d\"];\n",
			(int)this, thread_send, t->host.c_str(), t->prog.c_str(), t->tid);
	}
	//fprintf(fp, "r%x -> s%x;\n", (int)pred, (int)this);
}

void PathMessageSend::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message_send size=\"%d\" send=\"%d\" recv=\"%d\" ts=\"%ld.%06ld\" addr=\"%p\"/>\n",
		depth*2, "", size, thread_send, recv->thread_recv, ts_send.tv_sec, ts_send.tv_usec, this);
}

PathMessageRecv::PathMessageRecv(const MYSQL_ROW &row) : send(NULL) {
	// pathid = atoi(row[0]);
	// roles = row[1][0] ? strdup(row[1]) : NULL;
	// level = atoi(row[2]);
	// msgid is row[3]
	// ts_send is row[4]
	ts_recv = tv(strtoll(row[5], NULL, 10));
	// size is row[6]
	// thread_start is row[7]
	thread_recv = atoi(row[8]);
}

int PathMessageRecv::compare(const PathEvent *_other) const {
	if (_other->type() < PEV_MESSAGE_RECV) return -PCMP_NONE;
	if (_other->type() > PEV_MESSAGE_RECV) return PCMP_NONE;
	//const PathMessageRecv *other = (PathMessageRecv*)_other;
	// !! this will have to change if we allow thread-reordering
	// !! should check to make sure the right thread sends the msg
	// Messed up, same as PathMessageSend::compare.
	// assert(thread_recv == other->thread_recv);
	//if (send->thread_send < other->send->thread_send) return -PCMP_NONE;
	//if (send->thread_send > other->send->thread_send) return PCMP_NONE;

	return PCMP_EXACT;
}

std::string PathMessageRecv::to_string(void) const {
	char buf[256];
	snprintf(buf, sizeof(buf), "Recv(%d->%d, %d bytes)",
		send->thread_send, thread_recv, send->size);
	return std::string(buf);
}

void PathMessageRecv::print_dot(FILE *fp) const {
	PathThread *t = threads[thread_recv];
	fprintf(fp, "r%x [label = \"%d:%s/%s/%d\"];\n",
		(int)this, thread_recv, t->host.c_str(), t->prog.c_str(), t->tid);
	if (send->pred)
		fprintf(fp, "r%x -> r%x;\n", (int)send->pred, (int)this);
	else
		fprintf(fp, "s%x -> r%x;\n", (int)send, (int)this);
}

void PathMessageRecv::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message_recv send=\"%d\" recv=\"%d\" ts=\"%ld.%06ld\" send=\"%p\" />\n",
		depth*2, "", send->thread_send, thread_recv, ts_recv.tv_sec, ts_recv.tv_usec, send);
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
	size = messages = depth = 0;
	root_thread = -1;
	ts_start.tv_sec = ts_start.tv_usec = 0;
	ts_end.tv_sec = ts_end.tv_usec = 0;
}

Path::~Path(void) {
	std::map<int,PathEventList>::const_iterator thread;
	for (thread=children.begin(); thread!=children.end(); thread++)
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
	if (children.size() < other.children.size()) return -PCMP_NONE;
	if (children.size() > other.children.size()) return PCMP_NONE;
	std::map<int,PathEventList>::const_iterator cp, ocp;
	// !! we may have to try threads in all orders, not sure
	// it would be O(n^2), often less
	for (cp = children.begin(),ocp = other.children.begin();
			cp != children.end() && ocp != other.children.end();
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

void Path::print_dot(FILE *fp) const {
	fprintf(fp, "digraph foo {\n");
	for (std::map<int,PathEventList>::const_iterator list=children.begin(); list!=children.end(); list++) {
		for (unsigned int i=0; i<list->second.size(); i++)
			list->second[i]->print_dot(fp);
	}
	fprintf(fp, "}\n");
}

void Path::print(FILE *fp) const {
	for (std::map<int,PathEventList>::const_iterator list=children.begin(); list!=children.end(); list++) {
		fprintf(fp, "<thread id=\"%d\"%s>\n",
			list->first, list->first == root_thread ? " root=\"true\"" : "");
		for (unsigned int i=0; i<list->second.size(); i++)
			list->second[i]->print(fp, 1);
		fprintf(fp, "</thread>\n", list->first);
	}
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

static int message_depth(const PathEvent *pm) {
	PathEvent *pred;
	switch (pm->type()) {
		case PEV_MESSAGE_SEND:
			pred = ((PathMessageSend*)pm)->pred;
			return pred ? message_depth(pred) : 1;
		case PEV_MESSAGE_RECV:
			pred = ((PathMessageRecv*)pm)->send;
			return 1+message_depth(pred);
		default:
			fprintf(stderr, "message_depth: Invalid event type %d\n", pm->type());
			abort();
	}
}

static int max_message_depth(const PathEventList &list) {
	int max = 1;
	for (unsigned int i=0; i<list.size(); i++) {
		int depth = 0;
		switch (list[i]->type()) {
			case PEV_TASK:
				depth = max_message_depth(((PathTask*)list[i])->children);
				break;
			case PEV_MESSAGE_RECV:
				depth = message_depth(list[i]);
				break;
			default: ;
		}
		if (depth > max) max = depth;
	}
	return max;
}

void Path::done_inserting(void) {
	std::map<int,PathEventList>::iterator thread;

	if (children.size() == 0) {
		fprintf(stderr, "No threads -- empty path ??\n");
		return;
	}

	//fprintf(stderr, "Checking Path %d\n", path_id);
	for (thread=children.begin(); thread!=children.end(); thread++) {
		//fprintf(stderr, "  Checking Thread %d\n", thread->first);
		//for (unsigned int i=0; i<thread->second.size(); i++)
			//thread->second[i]->print(stderr, 2);
		check_order(thread->second);
	}

	// Can the DAG be connected?  Either all threads must have message
	// events, or there must be exactly one thread.
	if (children.size() > 1) {
		for (thread=children.begin(); thread!=children.end(); thread++) {
			int msg_count = count_messages(thread->second);
			if (msg_count == 0) {
				fprintf(stderr, "Malformed path -- unconnected causality DAG: thread %d has no messages\n", thread->first);
				return;
			}
		}

		// set all PathMessageSend.pred fields
		for (thread=children.begin(); thread!=children.end(); thread++)
			set_message_predecessors(thread->second, NULL);

		// find the root -- the only send without a predecessor
		PathMessageSend *root = NULL;
		for (thread=children.begin(); thread!=children.end(); thread++) {
			PathEvent *ev = first_message(thread->second);
			if (ev->type() == PEV_MESSAGE_SEND) {
				if (root != NULL) {
					fprintf(stderr, "Path %d has multiple roots!\n", path_id);
					ev->print(stderr, 1);
					return;
				}
				else {
					root = (PathMessageSend*)ev;
					assert(root->pred == NULL);
				}
			}
		}
		assert(root != NULL);
		root_thread = root->thread_send;
		//PathThread *root_thread = threads[((PathMessageSend*)ev)->thread_send];

		// !! make sure the DAG is connected -- do we touch all events?
		//
		// Hmmm, I think this is implied by finding a unique root.

	}
	else
		root_thread = children.begin()->first;

	assert(children.begin()->second.size() > 0);
	// !! root may not be the last to end
	// but anything else assumes synchronized clocks
	ts_start = children[root_thread][0]->start();
	ts_end = children[root_thread][children[root_thread].size()-1]->end();

	for (thread=children.begin(); thread!=children.end(); thread++)
		tally(thread->second, true);

	depth = 1;
	for (thread=children.begin(); thread!=children.end(); thread++) {
		int this_depth = max_message_depth(thread->second);
		if (this_depth > depth) depth = this_depth;
	}
}

void Path::tally(const PathEventList &list, bool toplevel) {
	for (unsigned int i=0; i<list.size(); i++) {
		const PathEvent *ev = list[i];
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
				tally(((PathTask*)ev)->children, false);
				break;
			case PEV_NOTICE:
				break;
			case PEV_MESSAGE_SEND:
				size += ((PathMessageSend*)ev)->size;
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
