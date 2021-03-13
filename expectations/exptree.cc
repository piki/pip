/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <stdarg.h>
#include <map>
#include <set>
#include <string>
#include "common.h"
#include "exptree.h"
#include "expect.tab.hh"

//#define DEBUG_FUTURES
#ifdef DEBUG_FUTURES
static int futures_depth;
#endif

static bool check_fragment(const PathEventList &test, const ExpEventList &list, unsigned int ofs, bool *resources,
		const FutureTable &ft, const FutureCounts &fc);
static void add_statement(const Node *node, ExpEventList *where);
static void add_statements(const ListNode *list_node, ExpEventList *where);
static void add_limits(const ListNode *limit_list, LimitList *limits);
static bool debug_failed_matches_individually = false;
#if 0
static void strsetf(std::string &str, const char *fmt, ...);
#endif
static void strappendf(std::string &str, const char *fmt, ...);
#ifdef DEBUG_FUTURES
static void print_match_set(FILE *fp, const MatchSet &set, int indent);
#endif

Match *Match::create(Node *node, bool negate) {
	if (node->type() == NODE_STRING)
		return new StringMatch(dynamic_cast<StringNode*>(node)->s, negate);
	else if (node->type() == NODE_REGEX)
		return new RegexMatch(dynamic_cast<StringNode*>(node)->s, negate);
	else if (node->type() == NODE_IDENTIFIER)
		return new VarMatch(dynamic_cast<IdentifierNode*>(node)->sym, negate);
	else if (node->type() == NODE_WILDCARD)
		return new AnyMatch(negate);
	else if (node->type() == NODE_OPERATOR) {
		OperatorNode *onode = dynamic_cast<OperatorNode*>(node);
		switch (onode->op) {
			case '!':
				assert(onode->nops() == 1);
				return create(onode->operands[0], !negate);
				break;
			default:
				fprintf(stderr, "invalid operator when expecting string: %s\n", get_op_name(onode->op));
				abort();
		}
	}
	else {
		fprintf(stderr, "invalid type where string_expr expected: %d\n",
			node->type());
		abort();
		return NULL;
	}
}

RegexMatch::RegexMatch(const std::string &_data, bool _negate) : Match(_negate), data(_data) {
	const char *err;
	int errofs;
	regex = pcre_compile(_data.c_str(), 0, &err, &errofs, NULL);
	if (!regex) {
		fprintf(stderr, "Error compiling regex: %s\n", err);
		fprintf(stderr, "%s\n%*s^\n", _data.c_str(), errofs, "");
		exit(1);
	}
}
RegexMatch::~RegexMatch(void) {
	assert(regex);
	free(regex);
}
const char *RegexMatch::to_string(void) const {
	static char ret[64];
	sprintf(ret, "m/%s/", data.c_str());
	return ret;
}
bool RegexMatch::check(const std::string &text) const {
	assert(regex);
	int ovector[30];
	bool res = pcre_exec(regex, NULL, text.c_str(), text.length(), 0, 0, ovector, 30) >= 0;
	return res;
}
bool StringMatch::check(const std::string &text) const { bool ret = data == text; return negate ? !ret : ret; }
const char *StringMatch::to_string(void) const {
	static char ret[64];
	sprintf(ret, "\"%s\"", data.c_str());
	return ret;
}
bool VarMatch::check(const std::string &text) const { return true; }  // !!

static const char *metric_name[] = {
	"REAL_TIME", "UTIME", "STIME", "CPU_TIME", "BUSY_TIME", "MAJOR_FAULTS",
	"MINOR_FAULTS", "VOL_CS", "INVOL_CS", "LATENCY", "SIZE", "MESSAGES",
	"DEPTH", "THREADS", "HOSTS", NULL
};

Limit::Metric Limit::metric_by_name(const std::string &name) {
	for (int i=0; metric_name[i]; i++)
		if (name == metric_name[i])
			return (Limit::Metric)i;
	fprintf(stderr, "Invalid metric \"%s\"\n", name.c_str());
	abort();
}

static float get_range_value(Node *n) {
	if (n) {
		switch (n->type()) {
			case NODE_UNITS:  return dynamic_cast<UnitsNode*>(n)->amt;
			case NODE_INT:    return dynamic_cast<IntNode*>(n)->value;
			case NODE_FLOAT:  return dynamic_cast<FloatNode*>(n)->value;
			default:
				fprintf(stderr, "Limit: expected unit, int, or float, got %d\n", n->type());
				abort();
		}
	}
	else
		return -1;
}

Limit::Limit(const OperatorNode *onode) : metric(LAST) {
	assert(onode->op == LIMIT);
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);
	metric = metric_by_name(dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name);

	assert(onode->operands[1]->type() == NODE_OPERATOR);
	OperatorNode *range = dynamic_cast<OperatorNode*>(onode->operands[1]);
	assert(range->op == RANGE);

	switch (range->nops()) {
		case 1:
			min = max = get_range_value(range->operands[0]);
			break;
		case 2:
			min = get_range_value(range->operands[0]);
			assert(min == -1 || min >= 0);
			max = get_range_value(range->operands[1]);
			assert(max == -1 || max >= min);
			break;
		default:
			fprintf(stderr, "Limit: invalid number of operands: %d\n", range->nops());
			abort();
	}
}

void Limit::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<limit metric=\"%s\" min=\"%f\" max=\"%f\" />\n",
		depth*2, "", metric_name[(int)metric], min, max);
}

bool Limit::check(const PathTask *test) const {
	switch (metric) {
		case REAL_TIME:      return check(test->tdiff);
		case UTIME:          return check(test->utime);
		case STIME:          return check(test->stime);
		case CPU_TIME:       return check(test->utime + test->stime);
		case BUSY_TIME:      return check((test->utime + test->stime) / (double)test->tdiff);
		case MAJOR_FAULTS:   return check(test->major_fault);
		case MINOR_FAULTS:   return check(test->minor_fault);
		case VOL_CS:         return check(test->vol_cs);
		case INVOL_CS:       return check(test->invol_cs);
		case LATENCY:
		case SIZE:
		case MESSAGES:
		case DEPTH:
		case THREADS:
		case HOSTS:
		default:
			fprintf(stderr, "Metric %s (%d) invalid or unknown when checking Task\n",
				metric_name[metric], metric);
			exit(1);
	}
}

bool Limit::check(const PathMessageSend *test) const {
	switch (metric) {
		// !!
		//case LATENCY:    return check(test->recv->ts_recv - test->ts_send);
		case SIZE:       return check(test->size);
		case REAL_TIME:
		case UTIME:
		case STIME:
		case CPU_TIME:
		case BUSY_TIME:
		case MAJOR_FAULTS:
		case MINOR_FAULTS:
		case VOL_CS:
		case INVOL_CS:
		case MESSAGES:
		case DEPTH:
		case THREADS:
		case HOSTS:
		default:
			fprintf(stderr, "Metric %s (%d) invalid or unknown when checking Message\n",
				metric_name[metric], metric);
			exit(1);
	}
}

bool Limit::check(const Path *test) const {
	switch (metric) {
		case REAL_TIME:      return check(test->ts_end - test->ts_start);
		case UTIME:          return check(test->utime);
		case STIME:          return check(test->stime);
		case CPU_TIME:       return check(test->utime + test->stime);
		case BUSY_TIME:      return check((test->utime + test->stime)/(double)(test->ts_end - test->ts_start));
		case MAJOR_FAULTS:   return check(test->major_fault);
		case MINOR_FAULTS:   return check(test->minor_fault);
		case VOL_CS:         return check(test->vol_cs);
		case INVOL_CS:       return check(test->invol_cs);
		case SIZE:           return check(test->size);
		case DEPTH:          return check(test->depth);
		case MESSAGES:       return check(test->messages);
		case THREADS:        return check(test->thread_pools.size());
		case HOSTS:          return check(test->hosts);
		case LATENCY:        return check(test->ts_end - test->ts_start);
		default:
			fprintf(stderr, "Metric %s (%d) unknown when checking Path\n",
				metric_name[metric], metric);
			exit(1);
	}
}

bool ExpEvent::base_check(const PathEventList &test, unsigned int ofs, int needed,
		PathEventType type, const char *name) const {
	// make sure there are enough objects to check
	if (ofs + needed > test.size()) {
		if (debug_failed_matches_individually)
			strappendf(debug_buffer, "      %s has nothing left to match.\n", name);
		return false;
	}

	// check type
	if (test[ofs]->type() != type) {
		if (debug_failed_matches_individually)
			strappendf(debug_buffer, "      %s expectation does not match %s event.\n", name, path_type_name[test[ofs]->type()]);
		return false;
	}

	return true;
}

ExpThread::ExpThread(const OperatorNode *onode) {
	assert(onode->type() == NODE_OPERATOR);
	assert(onode->op == THREAD);
	assert(onode->nops() == 5);  // name, where, count-range, limits, statements

	assert(onode->operands[2]->type() == NODE_OPERATOR);
	const OperatorNode *range = dynamic_cast<OperatorNode*>(onode->operands[2]);
	assert(range->op == RANGE);
	assert(range->nops() == 2);
	assert(range->operands[0]->type() == NODE_INT);
	min = dynamic_cast<IntNode*>(range->operands[0])->value;
	assert(range->operands[1]->type() == NODE_INT);
	max = dynamic_cast<IntNode*>(range->operands[1])->value;
	assert(max >= min);

	add_limits(dynamic_cast<ListNode*>(onode->operands[3]), &limits);
	assert(onode->operands[3]->type() == NODE_LIST);
	add_statements(dynamic_cast<ListNode*>(onode->operands[4]), &events);
	find_futures(events);
}

ExpThread::ExpThread(const ListNode *limit_list, const ListNode *statements) {
	min = 0;
	max = 1<<30;
	count = 0;

	assert(limit_list->type() == NODE_LIST);
	assert(statements->type() == NODE_LIST);
	add_limits(limit_list, &limits);
	add_statements(statements, &events);
	find_futures(events);
}

ExpThread::~ExpThread(void) {
	unsigned int i;
	for (i=0; i<events.size(); i++) delete events[i];
	for (i=0; i<limits.size(); i++) delete limits[i];
}

void ExpThread::print(FILE *fp) const {
	unsigned int i;
	for (i=0; i<events.size(); i++)
		events[i]->print(fp, 2);
}

bool ExpThread::check(const PathEventList &test, unsigned int ofs, bool fragment, bool *resources) {
	// !! check limits
	bool match;
	FutureCounts fc(ft.count());
	if (fragment)
		match = check_fragment(test, events, ofs, resources, ft, fc);
	else {
		MatchSet subset = PathRecognizer::vlistmatch(events, 0, test, ofs, ft, fc, true, true, true, true);
		if (subset.empty())
			match = false;
		else {
			assert(subset.size() == 1);
			assert(subset.begin()->futures.total() == 0);
			assert(subset.begin()->count() == test.size());
			match = true;
			*resources = subset.begin()->resources();
		}
	}
	if (!match) return false;
	if (++count > max) {
		if (debug_failed_matches_individually)
			strappendf(debug_buffer, "      Too many thread matches: max=%d count=%d\n", max, count);
		return false;
	}
	return true;
}

void ExpThread::find_futures(ExpEventList &list) {
	int seq = 0;
	find_futures(list, &seq);
#ifdef DEBUG_FUTURES
	printf("find_futures: seq=%d ft.count=%d\n", seq, ft.count());
#endif
}

void ExpThread::find_futures(ExpEventList &list, int *seq) {
	for (unsigned int i=0; i<list.size(); i++) {
		switch (list[i]->type()) {
			unsigned int j;
			case EXP_FUTURE:
				dynamic_cast<ExpFuture*>(list[i])->ft_pos = (*seq)++;
				ft.add(dynamic_cast<ExpFuture*>(list[i]));
				find_futures(dynamic_cast<ExpFuture*>(list[i])->children, seq);
				break;
			case EXP_TASK:
				find_futures(dynamic_cast<ExpTask*>(list[i])->children, seq);
				break;
			case EXP_XOR:
				for (j=0; j<dynamic_cast<ExpXor*>(list[i])->branches.size(); j++)
					find_futures(dynamic_cast<ExpXor*>(list[i])->branches[j], seq);
				break;
			case EXP_REPEAT:
				find_futures(dynamic_cast<ExpRepeat*>(list[i])->children, seq);
				break;
			case EXP_DONE:
				break;
			case EXP_CALL:{
					ExpCall *call = dynamic_cast<ExpCall*>(list[i]);
					PathRecognizer *called = dynamic_cast<PathRecognizer*>(recognizers_by_name[call->target]);
					assert(!called->complete);
					assert(called->threads.size() == 1);
					find_futures(called->threads.begin()->second->events, seq);
				}
				break;
			default:
				break;
		}
	}
}

ExpTask::ExpTask(const OperatorNode *onode) {
	assert(onode->op == TASK);
	assert(onode->nops() == 3);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *task_decl = dynamic_cast<OperatorNode*>(onode->operands[0]);
	assert(task_decl->op == TASK);
	assert(task_decl->nops() == 1);

	name = Match::create(task_decl->operands[0], false);

	add_limits(dynamic_cast<ListNode*>(onode->operands[1]), &limits);

	if (onode->operands[2]) {
		assert(onode->operands[2]->type() == NODE_LIST);
		add_statements(dynamic_cast<ListNode*>(onode->operands[2]), &children);
	}
}

ExpTask::~ExpTask(void) {
	delete name;
	unsigned int i;
	for (i=0; i<children.size(); i++) delete children[i];
	for (i=0; i<limits.size(); i++) delete limits[i];
}

void ExpTask::print(FILE *fp, int depth) const {
	bool empty = children.size() == 0 && limits.size() == 0;

	fprintf(fp, "%*s<task name=\"%s\"%s",
		depth*2, "", name->to_string(), empty ? " />\n" : ">\n");
	if (!empty) {
		unsigned int i;
		for (i=0; i<limits.size(); i++)
			limits[i]->print(fp, depth+1);
		for (i=0; i<children.size(); i++)
			children[i]->print(fp, depth+1);
		fprintf(fp, "%*s</task>\n", depth*2, "");
	}
}

MatchSet ExpTask::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	bool resources = true;

	if (!base_check(test, ofs, 1, PEV_TASK, "Task")) return ret;

	// check name
	PathTask *pt = dynamic_cast<PathTask*>(test[ofs]);
	if (!name->check(pt->name)) {
		if (debug_failed_matches_individually)
			strappendf(debug_buffer, "      Task(%s) does not match \"%s\"\n", name->to_string(), pt->name);
		return ret;  // name
	}

	// check resources
	for (unsigned int i=0; i<limits.size(); i++)
		if (!limits[i]->check(pt)) {
			resources = false;
			break;
		}

	MatchSet subset = PathRecognizer::vlistmatch(children, 0, pt->children, 0, ft, fc, true, false, false, true);
	if (subset.empty() && debug_failed_matches_individually)
		strappendf(debug_buffer, "      Task(%s) children did not match\n", name->to_string());
	for (MatchSet::const_iterator subsetp=subset.begin(); subsetp!=subset.end(); subsetp++)
		ret.insert(ExpMatch(1, resources && subsetp->resources(), subsetp->futures));

	return ret;
}

ExpNotice::ExpNotice(const OperatorNode *onode) {
	assert(onode->op == NOTICE);
	assert(onode->nops() == 1);

	name = Match::create(onode->operands[0], false);
}

void ExpNotice::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" />\n",
		depth*2, "", name->to_string());
}

MatchSet ExpNotice::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;

	if (!base_check(test, ofs, 1, PEV_NOTICE, "Notice")) return ret;

	// check name
	PathNotice *pn = dynamic_cast<PathNotice*>(test[ofs]);
	if (!name->check(pn->name)) {
		if (debug_failed_matches_individually)
			strappendf(debug_buffer, "      Notice(%s) does not match \"%s\"\n", name->to_string(), pn->name);
		return ret;  // name
	}

	ret.insert(ExpMatch(1, true, fc));
	return ret;
}

ExpMessageSend::ExpMessageSend(const OperatorNode *onode) {
	assert(onode->op == SEND);
	assert(onode->nops() == 2);

	ListNode *lnode;
	IdentifierNode *inode;
	switch (onode->operands[0]->type()) {
		case NODE_LIST:
			lnode = dynamic_cast<ListNode*>(onode->operands[0]);
			assert(lnode->size() >= 1);
			for (unsigned int i=0; i<lnode->size(); i++) ;
				//!! we're supposed to care about where Send goes
			break;
		case NODE_IDENTIFIER:
			inode = dynamic_cast<IdentifierNode*>(onode->operands[0]);
			//!! we're supposed to care about where Send goes
			break;
		default:
			assert(!"invalid argument for recv");
	}
	if (onode->operands[1])
		add_limits(dynamic_cast<ListNode*>(onode->operands[1]), &limits);
}

ExpMessageSend::~ExpMessageSend(void) {
	for (unsigned int i=0; i<limits.size(); i++)
		delete limits[i];
}

void ExpMessageSend::print(FILE *fp, int depth) const {
	bool empty = limits.size() == 0;
	fprintf(fp, "%*s<message_send%s", depth*2, "", empty ? " />\n" : ">\n");
	if (!empty) {
		for (unsigned int i=0; i<limits.size(); i++)
			limits[i]->print(fp, depth+1);
		fprintf(fp, "%*s</message_send>\n", depth*2, "");
	}
}

MatchSet ExpMessageSend::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	bool resources = true;

	if (!base_check(test, ofs, 1, PEV_MESSAGE_SEND, "Send")) return ret;

	PathMessageSend *pms = dynamic_cast<PathMessageSend*>(test[ofs]);

	// check resources
	for (unsigned int i=0; i<limits.size(); i++)
		if (!limits[i]->check(pms)) {
			resources = false;
			break;
		}

	// !! make sure thread-class matches

	ret.insert(ExpMatch(1, resources, fc));
	return ret;
}

ExpMessageRecv::ExpMessageRecv(const OperatorNode *onode) {
	assert(onode->op == RECV);
	assert(onode->nops() == 2);
	ListNode *lnode;
	IdentifierNode *inode;
	switch (onode->operands[0]->type()) {
		case NODE_LIST:
			lnode = dynamic_cast<ListNode*>(onode->operands[0]);
			assert(lnode->size() >= 1);
			for (unsigned int i=0; i<lnode->size(); i++) ;
				//!! we're supposed to care about where Recv goes
			break;
		case NODE_IDENTIFIER:
			inode = dynamic_cast<IdentifierNode*>(onode->operands[0]);
			//!! we're supposed to care about where Recv goes
			break;
		default:
			assert(!"invalid argument for recv");
	}
	if (onode->operands[1])
		add_limits(dynamic_cast<ListNode*>(onode->operands[1]), &limits);
}

void ExpMessageRecv::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message_recv />\n", depth*2, "");
}

MatchSet ExpMessageRecv::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	bool resources = true;

	if (!base_check(test, ofs, 1, PEV_MESSAGE_RECV, "Recv")) return ret;

	// !! make sure thread-class matches

	ret.insert(ExpMatch(1, resources, fc));
	return ret;
}

ExpRepeat::ExpRepeat(const OperatorNode *onode) {
	assert(onode->op == REPEAT);
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *range = dynamic_cast<OperatorNode*>(onode->operands[0]);
	assert(range->op == RANGE);
	assert(range->nops() == 2);

	if (range->operands[0]->type() == NODE_IDENTIFIER)
		assert(dynamic_cast<IdentifierNode*>(range->operands[0])->sym->type == SYM_INT_VAR);
	if (range->operands[1]->type() == NODE_IDENTIFIER)
		assert(dynamic_cast<IdentifierNode*>(range->operands[1])->sym->type == SYM_INT_VAR);

	min = range->operands[0];
	max = range->operands[1];

	add_statement(onode->operands[1], &children);
}

ExpRepeat::~ExpRepeat(void) {
	for (unsigned int i=0; i<children.size(); i++)
		delete children[i];
}

void ExpRepeat::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<repeat min=\"", depth*2, "");
	print_assert(fp, min);
	fprintf(fp, "\" max=\"");
	print_assert(fp, max);
	fprintf(fp, "\">\n");
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, depth+1);
	fprintf(fp, "%*s</repeat>\n", depth*2, "");
}

MatchSet ExpRepeat::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	int count;
	MatchSet ret, prev;
	prev.insert(ExpMatch(0, true, fc));
	int min_val = min->int_value();
	int max_val = max->int_value();
	for (count=0; count<max_val; count++) {
		if (count >= min_val) ret.insert(prev.begin(), prev.end());
		MatchSet newset;
		MatchSet::const_iterator prevp, curp;
		for (prevp=prev.begin(); prevp!=prev.end(); prevp++) {
			MatchSet subset = PathRecognizer::vlistmatch(children, 0, test, ofs + prevp->count(), ft, prevp->futures, false, false, false, true);
			for (curp=subset.begin(); curp!=subset.end(); curp++)
				newset.insert(ExpMatch(prevp->count() + curp->count(), prevp->resources() && curp->resources(), curp->futures));
		}
		if (newset.empty()) break;
		prev.swap(newset);
	}
	if (count == max_val) // reached the end
		ret.insert(prev.begin(), prev.end());
	if (count < min_val && debug_failed_matches_individually)
		strappendf(debug_buffer, "      Repeat(%d, %d) matched %d times\n", min, max, count);
	return ret;
}

ExpXor::ExpXor(const OperatorNode *onode) {
	assert(onode->op == XOR);
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_LIST);

	ListNode *lnode = dynamic_cast<ListNode*>(onode->operands[0]);
	for (unsigned int j=0; j<lnode->size(); j++) {
		assert((*lnode)[j]->type() == NODE_OPERATOR);
		const OperatorNode *branch = dynamic_cast<const OperatorNode*>((*lnode)[j]);
		assert(branch->op == BRANCH);
		assert(branch->nops() == 2);
		assert(branch->operands[0] == NULL);  // no named branches
		assert(branch->operands[1]->type() == NODE_LIST);
		branches.push_back(ExpEventList());
		add_statements(dynamic_cast<ListNode*>(branch->operands[1]), &branches[j]);
	}
}

ExpXor::~ExpXor(void) {
	for (unsigned int i=0; i<branches.size(); i++)
		for (unsigned int j=0; j<branches[i].size(); j++)
			delete branches[i][j];
}

void ExpXor::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<xor>\n", depth*2, "");
	for (unsigned int i=0; i<branches.size(); i++) {
		printf("%*s<branch>\n", (depth+1)*2, "");
		for (unsigned int j=0; j<branches[i].size(); j++)
			branches[i][j]->print(fp, depth+2);
		printf("%*s</branch>\n", (depth+1)*2, "");
	}
	fprintf(fp, "%*s</xor>\n", depth*2, "");
}

MatchSet ExpXor::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	for (unsigned int i=0; i<branches.size(); i++) {
		MatchSet subset = PathRecognizer::vlistmatch(branches[i], 0, test, ofs, ft, fc, false, false, false, true);
		ret.insert(subset.begin(), subset.end());
	}
	if (debug_failed_matches_individually && ret.empty())
		strappendf(debug_buffer, "      Xor(%d branches) had no match.\n", branches.size());
	return ret;
}

ExpAny::ExpAny(const OperatorNode *onode) {
	assert(onode->op == ANY);
	assert(onode->nops() == 0);
}

void ExpAny::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<any />\n", depth*2, "");
}

MatchSet ExpAny::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	for (int i=test.size()-ofs; i>=0; i--)
		ret.insert(ExpMatch(i, true, fc));
	return ret;
}

ExpAssign::ExpAssign(const OperatorNode *onode) {
	assert(onode->op == '=');
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);
	ident = dynamic_cast<IdentifierNode*>(onode->operands[0]);
	add_statement(onode->operands[1], &child);
}

ExpAssign::~ExpAssign(void) {
	delete ident;
	assert(child.size() == 1);
	delete child[0];
}

void ExpAssign::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<assign var=\"%s\">\n", depth*2, "", ident->sym->name.c_str());
	assert(child.size() == 1);
	child[0]->print(fp, depth+1);
	fprintf(fp, "%*s</assign>\n", depth*2, "");
}

MatchSet ExpAssign::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	//!! we need to get a return value from the child.
	//This actually means changing the match-search again.  MatchSets will
	//have to include the allowable value(s) for variables, paired up with
	//the number of statements matched.  Icky.
	MatchSet ret = PathRecognizer::vlistmatch(child, 0, test, ofs, ft, fc, false, false, false, true);
	if (debug_failed_matches_individually && ret.empty())
		strappendf(debug_buffer, "      Repeat had no match.\n");
	return ret;
}

ExpEval::ExpEval(const Node *node) {
	this->node = node;
}

ExpEval::~ExpEval(void) {
	delete node;
}

void ExpEval::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<eval expr=\"", depth*2, "");
	print_assert(fp, node);
	fprintf(fp, "\" />\n");
}

MatchSet ExpEval::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	//!! we need to do something with this return value
	//This actually means changing the match-search again.  MatchSets will
	//have to include the allowable value(s) for variables, paired up with
	//the number of statements matched.  Icky.
	MatchSet ret;
	ret.insert(ExpMatch(1, true, fc));
	return ret;
}

ExpFuture::ExpFuture(const OperatorNode *onode) : ft_pos(-1) {
	assert(onode->op == FUTURE);
	assert(onode->nops() == 2);
	if (onode->operands[0] != NULL) {
		assert(onode->operands[0]->type() == NODE_IDENTIFIER);
		name = dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name;
	}

	add_statement(onode->operands[1], &children);
}

ExpFuture::~ExpFuture(void) {
	for (unsigned int i=0; i<children.size(); i++)
		delete children[i];
}

void ExpFuture::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<future name=\"%s\">\n", depth*2, "", name.c_str());
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, depth+1);
	fprintf(fp, "%*s</future>\n", depth*2, "");
}

MatchSet ExpFuture::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	ExpMatch M(0, true, fc);
	assert(ft_pos != -1);
	M.futures.inc(ft_pos);
	ret.insert(M);
	return ret;
}

ExpDone::ExpDone(const OperatorNode *onode) {
	assert(onode->op == DONE);
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);
	name = dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name;
}

void ExpDone::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<done name=\"%s\" />\n", depth*2, "", name.c_str());
}

MatchSet ExpDone::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	MatchSet ret;
	ret.insert(ExpMatch(0, true, fc));
	//!! iterate the future table and flush out everything left called "name"
	return ret;
}

ExpCall::ExpCall(const OperatorNode *onode) {
	assert(onode->op == CALL);
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);

	target = dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name;
}

void ExpCall::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<call target=\"%s\" />\n", depth*2, "", target.c_str());
}

MatchSet ExpCall::check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const {
	PathRecognizer *called = dynamic_cast<PathRecognizer*>(recognizers_by_name[target]);
	if (!called) {
		fprintf(stderr, "call(%s): recognizer not found\n", target.c_str());
		exit(1);
	}
	assert(!called->complete);
	assert(called->threads.size() == 1);
	return PathRecognizer::vlistmatch(called->threads.begin()->second->events, 0, test, ofs, ft, fc, false, false, false, true);
	// !! if the called recognizer sets whole-path limits, check those
}

RecognizerBase::RecognizerBase(const IdentifierNode *ident, int _pathtype)
		: pathtype(_pathtype), instances(0), unique(0) {
	assert(ident->type() == NODE_IDENTIFIER);
	assert(pathtype == VALIDATOR || pathtype == INVALIDATOR || pathtype == RECOGNIZER);
	name = ident->sym->name;
}

void RecognizerBase::tally(const Path *p) {
	instances++;
	// unique++ if path or hosts different !!
	real_time.add(p->ts_end - p->ts_start);
	utime.add(p->utime);
	stime.add(p->stime);
	cpu_time.add(p->utime + p->stime);
	busy_time.add((p->utime + p->stime) / (double)(p->ts_end - p->ts_start));
	major_fault.add(p->major_fault);
	minor_fault.add(p->minor_fault);
	vol_cs.add(p->vol_cs);
	invol_cs.add(p->invol_cs);
	latency.add(p->latency);
	size.add(p->size);
	messages.add(p->messages);
	depth.add(p->depth);
	hosts.add(p->hosts);
	threadcount.add(p->thread_pools.size());
}

SetRecognizer::SetRecognizer(const IdentifierNode *ident, const Node *_bool_expr, int _pathtype)
		: RecognizerBase(ident, _pathtype), bool_expr(_bool_expr) { }

void SetRecognizer::print(FILE *fp) const {
	fprintf(fp, "<recognizer name=\"%s\" type=\"%s\">\n",
		name.c_str(), path_type_to_string(pathtype));
	print_assert(fp, bool_expr);
	fprintf(fp, "\n</recognizer>\n");
}

static bool eval_path_bool(const Node *node, std::map<std::string, bool> *match_map) {
	const OperatorNode *onode;
	const IdentifierNode *ident;
	std::map<std::string, bool>::const_iterator where;
	switch (node->type()) {
		case NODE_OPERATOR:
			onode = dynamic_cast<const OperatorNode*>(node);
			switch (onode->op) {
				case B_AND:
					assert(onode->nops() == 2);
					return eval_path_bool(onode->operands[0], match_map) && eval_path_bool(onode->operands[1], match_map);
				case B_OR:
					assert(onode->nops() == 2);
					return eval_path_bool(onode->operands[0], match_map) || eval_path_bool(onode->operands[1], match_map);
				case IMPLIES:
					assert(onode->nops() == 2);
					return !eval_path_bool(onode->operands[0], match_map) || eval_path_bool(onode->operands[1], match_map);
				case '!':
					assert(onode->nops() == 1);
					return !eval_path_bool(onode->operands[0], match_map);
				default:
					fprintf(stderr, "Invalid operator in SetRecognizer boolean expression: %d\n", onode->op);
					exit(1);
			}
		case NODE_IDENTIFIER:
			ident = dynamic_cast<const IdentifierNode*>(node);
			where = match_map->find(ident->sym->name);
			assert(where != match_map->end());
			return where->second;
		default:
			fprintf(stderr, "Invalid node type in SetRecognizer boolean expression: %d\n", node->type());
			exit(1);
	}
}

bool SetRecognizer::check(const Path *p, bool *resources, std::map<std::string, bool> *match_map) {
	assert(match_map);
	assert(match_map->find(name) == match_map->end());
	*resources = true;
	bool ret = eval_path_bool(bool_expr, match_map);
	(*match_map)[name] = ret;
	return ret;
}


PathRecognizer::PathRecognizer(const IdentifierNode *ident, const ListNode *limit_list, const ListNode *statements,
		bool _complete, int _pathtype) : RecognizerBase(ident, _pathtype), root(NULL), complete(_complete) {
	unsigned int i;

	add_limits(limit_list, &limits);

	assert(statements->type() == NODE_LIST);
	if (complete)
		for (i=0; i<statements->size(); i++) {
			assert((*statements)[i]->type() == NODE_OPERATOR);
			const OperatorNode *onode = dynamic_cast<const OperatorNode*>((*statements)[i]);
			switch (onode->op) {
				case THREAD:
					assert(onode->nops() == 5);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					threads[dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name] = new ExpThread(onode);
					if (!root)
						root = threads[dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name];
					break;
				default:
					fprintf(stderr, "Invalid operator %s (%d)\n", get_op_name(onode->op), onode->op);
					abort();
			}
		}
	else
		root = threads["(fragment)"] = new ExpThread(limit_list, statements);

	assert(root);
	if (complete) assert(root->max == 1);  // first thread is the root, can only appear once
}

PathRecognizer::~PathRecognizer(void) {
	for (ExpThreadSet::iterator tp=threads.begin(); tp!=threads.end(); tp++)
		delete tp->second;
	for (unsigned int i=0; i<limits.size(); i++) delete limits[i];
}

void PathRecognizer::print(FILE *fp) const {
	fprintf(fp, "<recognizer name=\"%s\" type=\"%s\" complete=\"%s\">\n",
		name.c_str(), path_type_to_string(pathtype), complete?"true":"false");
	for (unsigned int i=0; i<limits.size(); i++)
		limits[i]->print(fp, 1);
	for (ExpThreadSet::const_iterator tp=threads.begin(); tp!=threads.end(); tp++) {
		fprintf(fp, "  <thread name=\"%s\">\n", tp->first.c_str());
		tp->second->print(fp);
		fprintf(fp, "  </thread>\n");
	}
	fprintf(fp, "</recognizer>\n");
}

// !! how do we handle futures in fragments?  is it even legal?
// Can we match something there the literal events and the future events
// aren't contiguous?
static bool check_fragment(const PathEventList &test, const ExpEventList &list, unsigned int ofs, bool *resources,
		const FutureTable &ft, const FutureCounts &fc) {
	for (unsigned int i=0; i<test.size(); i++) {
		// !! This requires any futures to be satisfied immediately after (or
		// interspersed with) the normal expevents.  Better semantics would
		// allow the futures to be satisfied any time after.
		MatchSet matches = PathRecognizer::vlistmatch(list, 0, test, i, ft, fc, false, true, true, true);
		// set resources to false unless there's at least one true in "matches"
		if (resources) {
			*resources = false;
			MatchSet::const_iterator matchp;
			for (matchp=matches.begin(); matchp!=matches.end(); matchp++)
				if (matchp->resources()) {
					*resources = true;
					break;
				}
		}
		// !! should we keep looking for someone with resources true?
		if (!matches.empty()) return true;
		if (test[i]->type() == PEV_TASK)
			if (check_fragment(dynamic_cast<PathTask*>(test[i])->children, list, 0, resources, ft, fc)) return true;
	}
	return false;
}

bool debug_failed_matches = false;
std::string debug_buffer;
bool PathRecognizer::check(const Path *p, bool *resources, std::map<std::string, bool> *match_map) {
	debug_buffer.clear();
	if (match_map) {
		assert(match_map->find(name) == match_map->end());   // not inserted yet
		(*match_map)[name] = false;
	}
	if (!complete) {
		assert(threads.size() == 1);
		for (std::map<int,PathEventList>::const_iterator tp=p->thread_pools.begin(); tp!=p->thread_pools.end(); tp++)
			if (threads.begin()->second->check(tp->second, 0, true, resources)) {
				tally(p);
				if (match_map) (*match_map)[name] = true;
				return true;
			}
			else if (debug_failed_matches) {
				strappendf(debug_buffer, "Recognizer %s\n", name.c_str());
				strappendf(debug_buffer, "  P.thread[%d] did not match anything.  Let's see why...\n", tp->first);
				debug_failed_matches_individually = true;
				threads.begin()->second->check(tp->second, 0, true, resources);
				debug_failed_matches_individually = false;
			}
		return false;
	}

	// optimization: return false quickly if the number of threads is out of
	// range
	unsigned int min = 0, max = 0;
	for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++) {
		min += etp->second->min;
		max += etp->second->max;
	}
	if (p->thread_pools.size() < min || p->thread_pools.size() > max) {
		if (debug_failed_matches) {
			strappendf(debug_buffer, "Recognizer %s\n", name.c_str());
			if (min == max)
				strappendf(debug_buffer, "  false: recognizer wants between %d thread%s, path has %d thread%s\n",
				min, min==1?"":"s", p->thread_pools.size(), p->thread_pools.size()==1?"":"s");
			else
				strappendf(debug_buffer, "  false: recognizer wants between %d and %d threads, path has %d thread%s\n",
				min, max, p->thread_pools.size(), p->thread_pools.size()==1?"":"s");
		}
		return false;
	}

	// check path-wide resource limits
	if (resources)
		for (unsigned int i=0; i<limits.size(); i++)
			if (!limits[i]->check(p)) {
				*resources = false;
				break;
			}

	assert(p->root_thread != -1);
	std::set<int> path_threads_used;
	for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++)
		etp->second->count = 0;

	// check the root first.  we know which exp thread and which path thread
	// are the roots, so we can check here and exclude them from the loop.
	if (!root->check(p->thread_pools.find(p->root_thread)->second, 0, false, resources)) {
		if (debug_failed_matches) {
			strappendf(debug_buffer, "Recognizer %s\n", name.c_str());
			strappendf(debug_buffer, "  P.root (tid=%d) does not match R.root\n", p->root_thread);
			debug_failed_matches_individually = true;
			(void)root->check(p->thread_pools.find(p->root_thread)->second, 0, false, resources);
			debug_failed_matches_individually = false;
		}
		return false;
	}

	// for each path thread PT
	//   for each exp thread ET
	//     if ET.match(PT) match = true
	//   if !match return false
	for (std::map<int,PathEventList>::const_iterator ptp=p->thread_pools.begin(); ptp!=p->thread_pools.end(); ptp++) {
		if (ptp->first == p->root_thread) continue;
		bool matched = false;
		for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++) {
			if (etp->second == root) continue;
			if (etp->second->check(ptp->second, 0, false, resources)) {
				matched = true;
				break;
			}
		}
		if (!matched) {
			if (debug_failed_matches) {
				strappendf(debug_buffer, "Recognizer %s\n", name.c_str());
				strappendf(debug_buffer, "  P.thread[%d] did not match anything.  Let's see why...\n", ptp->first);
				debug_failed_matches_individually = true;
				for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++) {
					if (etp->second == root) continue;
					strappendf(debug_buffer, "    R.thread %s...\n", etp->first.c_str());
					if (etp->second->check(ptp->second, 0, false, resources)) continue;
				}
				debug_failed_matches_individually = false;
			}
			return false;
		}
	}

	// are the thread counts all within range?
	for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++) {
		if (etp->second->count > etp->second->max) {
			if (debug_failed_matches)
				strappendf(debug_buffer, "Rejected because there were too many of thread \"%s\"\n", etp->first.c_str());
			return false;
		}
		if (etp->second->count < etp->second->min) {
			if (debug_failed_matches)
				strappendf(debug_buffer, "Rejected because there weren't enough of thread \"%s\"\n", etp->first.c_str());
			return false;
		}
	}

	// it matches!  tally up resources and return success
	tally(p);
	if (match_map)
		(*match_map)[name] = *resources;   // !! or should we just say "true" and include everything that matches structure?
	// ... or have the match_map contain both ret and resources?
	return true;
}

// returns true if caller should return "ret" immediately
static bool vlistmatch_helper(const MatchSet &subset, MatchSet &ret,
		const ExpEventList &list, int eofs, const PathEventList &test, int pofs,
		const FutureTable &ft, bool match_all, bool match_all_futures, bool find_one, bool allow_futures) {
	//printf("subset.size=%d\n", subset.size());
	//print_match_set(stdout, subset, eofs);
	MatchSet::const_iterator subsetp;
	// !! prefer the ones with resources true?
	for (subsetp=subset.begin(); subsetp!=subset.end(); subsetp++) {
		MatchSet after = PathRecognizer::vlistmatch(list, eofs, test,
			pofs+subsetp->count(), ft, subsetp->futures, match_all, match_all_futures,
			find_one, allow_futures);
		if (find_one) {
			if (!after.empty()) {
				assert(after.size() == 1);
				assert(ret.empty());
				ret.insert(ExpMatch(subsetp->count() + after.begin()->count(),
					subsetp->resources() && after.begin()->resources(), after.begin()->futures));
				return true;
			}
		}
		else
			for (MatchSet::const_iterator afterp=after.begin(); afterp!=after.end(); afterp++)
				ret.insert(ExpMatch(subsetp->count() + afterp->count(),
					subsetp->resources() && afterp->resources(), afterp->futures));
	}
	return false;
}

MatchSet PathRecognizer::vlistmatch(const ExpEventList &list, unsigned int eofs, const PathEventList &test,
		unsigned int pofs, const FutureTable &ft, const FutureCounts &fc, bool match_all, bool match_all_futures, bool find_one,
		bool allow_futures) {
	assert(eofs <= list.size());
	assert(pofs <= test.size());
	if (match_all_futures) assert(allow_futures);
	MatchSet ret;

	// are we done?
	if (eofs == list.size()                          // done with expectations
			&& (!match_all_futures || fc.total() == 0)   // done with futures
			&& (!match_all || pofs == test.size())) {    // done consuming path events
		ret.insert(ExpMatch(0, true, fc));
		if (find_one) return ret;
	}

#ifdef DEBUG_FUTURES
	printf("%*svlistmatch(eofs=%d, pofs=%d, fc=[", futures_depth++*2, "", eofs, pofs);
	for (unsigned int x=0; x<ft.count(); x++) printf(" %d", fc[x]);
	printf(" ], %smatch_all, %smatch_all_futures, %sfind_one, %sallow_futures) {\n", match_all?"":"!", match_all_futures?"":"!", find_one?"":"!", allow_futures?"":"!");
#endif

	// check the next event, if there is one
	if (eofs < list.size()) {
#ifdef DEBUG_FUTURES
		printf("%*sTrying the next event:\n", futures_depth*2, "");
		list[eofs]->print(stdout, futures_depth);
		if (pofs != test.size()) test[pofs]->print(stdout, futures_depth); else printf("%*s[empty]\n", futures_depth*2, "");
#endif
		MatchSet subset = list[eofs]->check(test, pofs, ft, fc);
#ifdef DEBUG_FUTURES
		print_match_set(stdout, subset, futures_depth);
#endif
		if (vlistmatch_helper(subset, ret, list, eofs+1, test, pofs, ft, match_all, match_all_futures, find_one, allow_futures)) {
#ifdef DEBUG_FUTURES
			printf("%*s} = ", --futures_depth*2, "");
			print_match_set(stdout, ret, 0);
#endif
			return ret;
		}
	}

	// check any available futures
	if (allow_futures)
		for (unsigned int i=0; i<ft.count(); i++)
			if (fc[i] > 0) {
#ifdef DEBUG_FUTURES
				printf("%*sFuture #%d:\n", futures_depth*2, "", i);
				ft[i]->print(stdout, futures_depth);
				if (pofs != test.size()) test[pofs]->print(stdout, futures_depth); else printf("%*s[empty]\n", futures_depth*2, "");
#endif
				FutureCounts sfc(fc);
				sfc.dec(i);
				MatchSet subset = vlistmatch(ft[i]->children, 0, test, pofs, ft, sfc, false, false, false, false);
				sfc.inc(i);
#ifdef DEBUG_FUTURES
				print_match_set(stdout, subset, futures_depth);
#endif
				if (vlistmatch_helper(subset, ret, list, eofs, test, pofs, ft, match_all, match_all_futures, find_one, allow_futures)) {
#ifdef DEBUG_FUTURES
					printf("%*s} = ", --futures_depth*2, "");
					print_match_set(stdout, ret, 0);
#endif
					return ret;
				}
			}

#ifdef DEBUG_FUTURES
	printf("%*s} = ", --futures_depth*2, "");
	print_match_set(stdout, ret, 0);
#endif
	return ret;
}

static void add_statement(const Node *node, ExpEventList *where) {
	switch (node->type()) {
		case NODE_LIST:
			add_statements(dynamic_cast<const ListNode*>(node), where);
			break;
		case NODE_STRING:
		case NODE_REGEX:
		case NODE_INT:
		case NODE_IDENTIFIER:
			where->push_back(new ExpEval(node));
			break;
		case NODE_OPERATOR:{
				const OperatorNode *onode = dynamic_cast<const OperatorNode*>(node);
				switch (onode->op) {
					case TASK:
						where->push_back(new ExpTask(onode));
						break;
					case NOTICE:
						where->push_back(new ExpNotice(onode));
						break;
					case REPEAT:
						where->push_back(new ExpRepeat(onode));
						break;
					case XOR:
						where->push_back(new ExpXor(onode));
						break;
					case CALL:
						where->push_back(new ExpCall(onode));
						break;
					case SEND:
						where->push_back(new ExpMessageSend(onode));
						break;
					case RECV:
						where->push_back(new ExpMessageRecv(onode));
						break;
					case ANY:
						where->push_back(new ExpAny(onode));
						break;
					case FUTURE:
						where->push_back(new ExpFuture(onode));
						break;
					case DONE:
						where->push_back(new ExpDone(onode));
						break;
					case '=':
						where->push_back(new ExpAssign(onode));
						break;
					case '+':
					case '-':
					case '*':
						where->push_back(new ExpEval(onode));
						break;
					default:
						fprintf(stderr, "unknown op %s (%d)\n", get_op_name(onode->op), onode->op);
						abort();
				}
			}
			break;
		default:
			fprintf(stderr, "Invalid node type %d when expecting statement\n",
				node->type());
			exit(1);
	}
}

static void add_statements(const ListNode *list_node, ExpEventList *where) {
	unsigned int i;
	assert(where != NULL);
	for (i=0; i<list_node->size(); i++)
		add_statement((*list_node)[i], where);
}

static void add_limits(const ListNode *limit_list, LimitList *limits) {
	assert(limit_list->type() == NODE_LIST);
	for (unsigned int i=0; i<limit_list->size(); i++) {
		const OperatorNode *stmt = dynamic_cast<const OperatorNode*>((*limit_list)[i]);
		if (stmt->op == LEVEL) {
			assert(stmt->nops() == 1);
			assert(stmt->operands[0]->type() == NODE_INT);
			// !! ignored...
		}
		else {
			assert(stmt->op == LIMIT);
			limits->push_back(new Limit(stmt));
		}
	}
}


const char *path_type_to_string(int pathtype) {
	switch (pathtype) {
		case VALIDATOR:   return "validator";
		case INVALIDATOR: return "invalidator";
		case RECOGNIZER:  return "recognizer";
		default: fprintf(stderr, "invalid path type: %d\n", pathtype); exit(1);
	}
}

#if 0
static void strsetf(std::string &str, const char *fmt, ...) {
	char buf[4096];
	va_list arg;
	va_start(arg, fmt);
	vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);
	str = buf;
}
#endif

static void strappendf(std::string &str, const char *fmt, ...) {
	char buf[4096];
	va_list arg;
	va_start(arg, fmt);
	vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);
	str.append(buf);
}

#ifdef DEBUG_FUTURES
static void print_match_set(FILE *fp, const MatchSet &set, int indent) {
	fflush(fp);
	fprintf(fp, "%*sprint_match_set(size=%d): ", indent*2, "", set.size());
	for (MatchSet::const_iterator setp=set.begin(); setp!=set.end(); setp++) {
		fprintf(fp, "[%d %s {", setp->count(), setp->resources()?"T":"F");
		for (int i=0; i<setp->futures.sz; i++)
			fprintf(fp, " %d", setp->futures[i]);
		fprintf(fp, " }] ");
	}
	fprintf(fp, "\n");
	fflush(fp);
}
#endif
