#include <assert.h>
#include "common.h"
#include "path.h"

inline static timeval tv(long long ts) {
	timeval ret;
	ret.tv_sec = ts/1000000;
	ret.tv_usec = ts%1000000;
	return ret;
}

PathTask::PathTask(const MYSQL_ROW &row) {
	name = strdup(row[1]);
	ts_start = tv(strtoll(row[2], NULL, 10));
	ts_end = tv(strtoll(row[3], NULL, 10));
	utime = atoi(row[4]);
	stime = atoi(row[5]);
	major_fault = atoi(row[6]);
	minor_fault = atoi(row[7]);
	vol_cs = atoi(row[8]);
	invol_cs = atoi(row[9]);
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
	name = strdup(row[1]);
	ts = tv(strtoll(row[2], NULL, 10));
}

void PathNotice::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" ts=\"%ld.%06ld\" />\n", depth*2, "",
		name, ts.tv_sec, ts.tv_usec);
}

void PathMessage::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message />\n", depth*2, "");
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

void Path::insert(PathTask *pt, std::vector<PathEvent *> &where) {
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
				insert(pt, ((PathTask*)where[i])->children);
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

void Path::insert(PathNotice *pn, std::vector<PathEvent *> &where) {
	for (unsigned int i=0; i<where.size(); i++) {
		if (where[i]->type() != PEV_TASK) continue;
		if (pn->ts < where[i]->start()) {      // before where[i]
			where.insert(where.begin()+i, pn);
			return;
		}
		if (pn->ts <= where[i]->end()) {       // inside where[i]
			insert(pn, ((PathTask*)where[i])->children);
			return;
		}
	}
	where.push_back(pn);
}

void Path::insert(PathMessage *pm, std::vector<PathEvent *> &where) {
	pm->print();
	assert(!"not implemented yet");
}

void Path::print(FILE *fp) const {
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, 0);
}

void Path::done_inserting(void) {
	if (children.size() == 0) return;
	ts_start = children[0]->start();
	ts_end = children[children.size()-1]->end();
	tally(&children);
}

void Path::tally(const PathEventList *list) {
	for (unsigned int i=0; i<list->size(); i++) {
		const PathEvent *ev = (*list)[i];
		switch (ev->type()) {
			case PEV_TASK:
				utime += ((PathTask*)ev)->utime;
				stime += ((PathTask*)ev)->stime;
				major_fault += ((PathTask*)ev)->major_fault;
				minor_fault += ((PathTask*)ev)->minor_fault;
				vol_cs += ((PathTask*)ev)->vol_cs;
				invol_cs += ((PathTask*)ev)->invol_cs;
				tally(&((PathTask*)ev)->children);
				break;
			case PEV_NOTICE:
				break;
			case PEV_MESSAGE:
				size += ((PathMessage*)ev)->size;
				break;
			default:
				assert(!"invalid PathEventType");
		}
	}
}
