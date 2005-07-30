#include <assert.h>
#include <map>
#include <set>
#include "common.h"
#include "exptree.h"
#include "expect.tab.hh"

static bool check_fragment(const PathEventList &test, const ExpEventList &list, int ofs, bool *resources);
static void add_statement(const Node *node, ExpEventList *where);
static void add_statements(const ListNode *list_node, ExpEventList *where);
static void add_limits(const ListNode *limit_list, LimitList *limits);
static bool debug_failed_matches_individually = false;

Match *Match::create(Node *node, bool negate) {
	if (node->type() == NODE_STRING)
		return new StringMatch(((StringNode*)node)->s, negate);
	else if (node->type() == NODE_REGEX)
		return new RegexMatch(((StringNode*)node)->s, negate);
	else if (node->type() == NODE_IDENTIFIER)
		return new VarMatch(((IdentifierNode*)node)->sym, negate);
	else if (node->type() == NODE_WILDCARD)
		return new AnyMatch(negate);
	else if (node->type() == NODE_OPERATOR) {
		OperatorNode *onode = (OperatorNode*)node;
		switch (onode->op) {
			case '=':
				assert(onode->nops() == 2);
				fprintf(stderr, "string_expr assignment not yet supported\n");
				return create((StringNode*)onode->operands[1], negate);
				break;
			case '!':
				assert(onode->nops() == 1);
				return create((StringNode*)onode->operands[0], !negate);
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
	//if (!res) printf("regex did not match \"%s\"\n", text.c_str());
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
			case NODE_UNITS:  return ((UnitsNode*)n)->amt;
			case NODE_INT:    return ((IntNode*)n)->value;
			case NODE_FLOAT:  return ((FloatNode*)n)->value;
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
	metric = metric_by_name(((IdentifierNode*)onode->operands[0])->sym->name);

	assert(onode->operands[1]->type() == NODE_OPERATOR);
	OperatorNode *range = (OperatorNode*)onode->operands[1];
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
		case THREADS:        return check(test->children.size());
		case HOSTS:          return check(test->hosts);
		case LATENCY:        return check(test->ts_end - test->ts_start);
		default:
			fprintf(stderr, "Metric %s (%d) unknown when checking Path\n",
				metric_name[metric], metric);
			exit(1);
	}
}

ExpThread::ExpThread(const OperatorNode *onode) {
	assert(onode->type() == NODE_OPERATOR);
	assert(onode->op == THREAD);
	assert(onode->nops() == 5);  // name, where, count-range, limits, statements

	assert(onode->operands[2]->type() == NODE_OPERATOR);
	const OperatorNode *range = (OperatorNode*)onode->operands[2];
	assert(range->op == RANGE);
	assert(range->nops() == 2);
	assert(range->operands[0]->type() == NODE_INT);
	min = ((IntNode*)range->operands[0])->value;
	assert(range->operands[1]->type() == NODE_INT);
	max = ((IntNode*)range->operands[1])->value;
	assert(max >= min);

	add_limits((ListNode*)onode->operands[3], &limits);
	assert(onode->operands[3]->type() == NODE_LIST);
	add_statements((ListNode*)onode->operands[4], &events);
}

ExpThread::ExpThread(const ListNode *limit_list, const ListNode *statements) {
	min = 0;
	max = 1<<30;
	count = 0;

	assert(limit_list->type() == NODE_LIST);
	assert(statements->type() == NODE_LIST);
	add_limits(limit_list, &limits);
	add_statements(statements, &events);
}

ExpThread::~ExpThread(void) {
	unsigned int i;
	for (i=0; i<events.size(); i++) delete events[i];
	for (i=0; i<limits.size(); i++) delete limits[i];
}

void ExpThread::print(FILE *fp) const {
	unsigned int i;
	for (i=0; i<events.size(); i++)
		events[i]->print(fp, 1);
}

bool ExpThread::check(const PathEventList &test, int ofs, bool fragment, bool *resources) {
	// !! check limits
	bool match;
	if (fragment)
		match = check_fragment(test, events, ofs, resources);
	else
		match = ofs+Recognizer::check(test, events, ofs, resources) == (int)test.size();
	if (match && ++count <= max) return true;
	return false;
}

ExpTask::ExpTask(const OperatorNode *onode) {
	assert(onode->op == TASK);
	assert(onode->nops() == 3);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *task_decl = (OperatorNode*)onode->operands[0];
	assert(task_decl->op == TASK);
	assert(task_decl->nops() == 1);

	name = Match::create((StringNode*)task_decl->operands[0], false);

	add_limits((ListNode*)onode->operands[1], &limits);

	if (onode->operands[2]) {
		assert(onode->operands[2]->type() == NODE_LIST);
		add_statements((ListNode*)onode->operands[2], &children);
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

int ExpTask::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) {
		if (debug_failed_matches_individually) printf("      Task(%s) has nothing left to match.\n", name->to_string());
		return -1;  // we need at least one
	}
	if (test[ofs]->type() != PEV_TASK) {
		if (debug_failed_matches_individually) printf("      Task(%s) expectation does not match %s event.\n", name->to_string(), path_type_name[test[ofs]->type()]);
		return -1;
	}
	PathTask *pt = (PathTask*)test[ofs];
	if (!name->check(pt->name)) {
		if (debug_failed_matches_individually) printf("      Task(%s) does not match \"%s\"\n", name->to_string(), pt->name);
		return -1;  // name
	}
	if (resources)
		for (unsigned int i=0; i<limits.size(); i++)
			if (!limits[i]->check(pt)) {
				*resources = false;
				break;
			}

	int matched_children = Recognizer::check(pt->children, children, 0, resources);
	assert(matched_children <= (int)pt->children.size());
	if (matched_children == -1) {
		if (debug_failed_matches_individually) printf("      Task(%s) children did not match\n", name->to_string());
		return -1;  // children
	}
	else if (matched_children < (int)pt->children.size()) {
		if (debug_failed_matches_individually) printf("      Task(%s) had %d leftover children\n",
			name->to_string(), pt->children.size() - matched_children);
		return -1;  // children
	}
	return 1;
}

ExpNotice::ExpNotice(const OperatorNode *onode) {
	assert(onode->op == NOTICE);
	assert(onode->nops() == 1);

	name = Match::create((StringNode*)onode->operands[0], false);
}

void ExpNotice::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" />\n",
		depth*2, "", name->to_string());
}

int ExpNotice::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) {
		if (debug_failed_matches_individually) printf("      Notice(%s) has nothing left to match.\n", name->to_string());
		return -1;  // we need at least one
	}
	if (test[ofs]->type() != PEV_NOTICE) {
		if (debug_failed_matches_individually) printf("      Notice(%s) expectation does not match %s event.\n", name->to_string(), path_type_name[test[ofs]->type()]);
		return -1;
	}
	PathNotice *pn = (PathNotice*)test[ofs];
	if (!name->check(pn->name)) {
		if (debug_failed_matches_individually) printf("      Notice(%s) does not match \"%s\"\n", name->to_string(), pn->name);
		return -1;  // name
	}
	return 1;
}

ExpMessageSend::ExpMessageSend(const OperatorNode *onode) {
	assert(onode->op == SEND);
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_LIST);
	ListNode *lnode = (ListNode*)onode->operands[0];
	assert(lnode->size() >= 1);
	for (unsigned int i=0; i<lnode->size(); i++) ;
		//!! we're supposed to care about where Send goes
	if (onode->operands[1])
		add_limits((ListNode*)onode->operands[1], &limits);
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

int ExpMessageSend::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) {
		if (debug_failed_matches_individually) printf("      Send has nothing left to match.\n");
		return -1;  // we need at least one
	}
	if (test[ofs]->type() != PEV_MESSAGE_SEND) {
		if (debug_failed_matches_individually) printf("      Send expectation does not match %s event.\n", path_type_name[test[ofs]->type()]);
		return -1;
	}
	PathMessageSend *pms = (PathMessageSend*)test[ofs];
	if (resources)
		for (unsigned int i=0; i<limits.size(); i++)
			if (!limits[i]->check(pms)) {
				*resources = false;
				break;
			}

	// !! make sure thread-class matches

	return 1;
}

ExpMessageRecv::ExpMessageRecv(const OperatorNode *onode) {
	assert(onode->op == RECV);
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_LIST);
	ListNode *lnode = (ListNode*)onode->operands[0];
	assert(lnode->size() >= 1);
	for (unsigned int i=0; i<lnode->size(); i++) ;
		//!! we're supposed to care about where Recv goes
	if (onode->operands[1])
		add_limits((ListNode*)onode->operands[1], &limits);
}

void ExpMessageRecv::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message_recv />\n", depth*2, "");
}

int ExpMessageRecv::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) {
		if (debug_failed_matches_individually) printf("      Recv has nothing left to match.\n");
		return -1;  // we need at least one
	}
	if (test[ofs]->type() != PEV_MESSAGE_RECV) {
		if (debug_failed_matches_individually) printf("      Recv expectation does not match %s event.\n", path_type_name[test[ofs]->type()]);
		return -1;
	}

	// !! make sure thread-class matches

	return 1;
}

ExpRepeat::ExpRepeat(const OperatorNode *onode) {
	assert(onode->op == REPEAT);
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *range = (OperatorNode*)onode->operands[0];
	assert(range->op == RANGE);
	assert(range->nops() == 2);
	assert(range->operands[0]->type() == NODE_INT);
	assert(range->operands[1]->type() == NODE_INT);

	min = ((IntNode*)range->operands[0])->value;
	max = ((IntNode*)range->operands[1])->value;

	add_statement(onode->operands[1], &children);
}

ExpRepeat::~ExpRepeat(void) {
	for (unsigned int i=0; i<children.size(); i++)
		delete children[i];
}

void ExpRepeat::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<repeat min=\"%d\" max=\"%d\">\n", depth*2, "", min, max);
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, depth+1);
	fprintf(fp, "%*s</repeat>\n", depth*2, "");
}

int ExpRepeat::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	int count, my_ofs=ofs;
	for (count=0; count<max; count++) {
		int res = Recognizer::check(test, children, my_ofs, resources);
		if (res == -1) break;
		my_ofs += res;
	}
	if (count >= min) return my_ofs - ofs;
	if (debug_failed_matches_individually)
		printf("      Repeat(%d, %d) matched %d times\n", min, max, count);
	return -1;
}

ExpXor::ExpXor(const OperatorNode *onode) {
	assert(onode->op == XOR);
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_LIST);

	ListNode *lnode = (ListNode*)onode->operands[0];
	for (unsigned int j=0; j<lnode->size(); j++) {
		assert((*lnode)[j]->type() == NODE_OPERATOR);
		OperatorNode *branch = (OperatorNode*)(*lnode)[j];
		assert(branch->op == BRANCH);
		assert(branch->nops() == 2);
		assert(branch->operands[0] == NULL);  // no named branches
		assert(branch->operands[1]->type() == NODE_LIST);
		branches.push_back(ExpEventList());
		add_statements((ListNode*)branch->operands[1], &branches[j]);
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

int ExpXor::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	for (unsigned int i=0; i<branches.size(); i++) {
		int res = Recognizer::check(test, branches[i], ofs, resources);
		if (res != -1) return res;  // !! greedy, continuation needed
	}
	if (debug_failed_matches_individually) printf("      Xor(%d branches) had no match.\n", branches.size());
	return -1;
}

ExpAny::ExpAny(const OperatorNode *onode) {
	assert(onode->op == ANY);
	assert(onode->nops() == 0);
}

void ExpAny::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<any />\n", depth*2, "");
}

int ExpAny::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	// !! greedy, continuation needed
	return test.size() - ofs;  // match them all!  even if there aren't any!
}

ExpCall::ExpCall(const OperatorNode *onode) {
	assert(onode->op == CALL);
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);

	target = ((IdentifierNode*)onode->operands[0])->sym->name;
}

void ExpCall::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<call target=\"%s\" />\n", depth*2, "", target.c_str());
}

int ExpCall::check(const PathEventList &test, unsigned int ofs, bool *resources) const {
	Recognizer *r = recognizers[target];
	if (!r) {
		fprintf(stderr, "call(%s): recognizer not found\n", target.c_str());
		exit(1);
	}
	assert(!r->complete);
	return r->check(test, r->threads.begin()->second->events, ofs, resources);
	// !! if the called recognizer sets whole-path limits, check those
}

Recognizer::Recognizer(const IdentifierNode *ident, const ListNode *limit_list, const ListNode *statements,
		bool _complete, int _pathtype)
		: root(NULL), complete(_complete), pathtype(_pathtype), instances(0), unique(0) {
	assert(ident->type() == NODE_IDENTIFIER);
	assert(pathtype == VALIDATOR || pathtype == INVALIDATOR || pathtype == RECOGNIZER);
	name = ident->sym->name;

	unsigned int i;

	add_limits(limit_list, &limits);

	assert(statements->type() == NODE_LIST);
	if (complete)
		for (i=0; i<statements->size(); i++) {
			assert((*statements)[i]->type() == NODE_OPERATOR);
			const OperatorNode *onode = (OperatorNode*)(*statements)[i];
			switch (onode->op) {
				case THREAD:
					assert(onode->nops() == 5);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					threads[((IdentifierNode*)onode->operands[0])->sym->name] = new ExpThread(onode);
					if (!root)
						root = threads[((IdentifierNode*)onode->operands[0])->sym->name];
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

Recognizer::~Recognizer(void) {
	for (ExpThreadSet::iterator tp=threads.begin(); tp!=threads.end(); tp++)
		delete tp->second;
	for (unsigned int i=0; i<limits.size(); i++) delete limits[i];
}

void Recognizer::print(FILE *fp) const {
	fprintf(fp, "<recognizer name=\"%s\" type=\"%s\" complete=\"%s\">\n",
		name.c_str(), path_type_to_string(pathtype), complete?"true":"false");
	for (unsigned int i=0; i<limits.size(); i++)
		limits[i]->print(fp, 1);
	for (ExpThreadSet::const_iterator tp=threads.begin(); tp!=threads.end(); tp++) {
		printf("  <!-- begin thread \"%s\" -->\n", tp->first.c_str());
		tp->second->print(fp);
	}
	fprintf(fp, "</recognizer>\n");
}

static bool check_fragment(const PathEventList &test, const ExpEventList &list, int ofs, bool *resources) {
	for (unsigned int i=0; i<test.size(); i++) {
		if (Recognizer::check(test, list, i, resources) != -1) return true;
		if (test[i]->type() == PEV_TASK)
			if (check_fragment(((PathTask*)test[i])->children, list, 0, resources)) return true;
	}
	return false;
}

bool debug_failed_matches = false;
bool Recognizer::check(const Path *p, bool *resources) {
	if (!complete) {
		assert(threads.size() == 1);
		for (std::map<int,PathEventList>::const_iterator tp=p->children.begin(); tp!=p->children.end(); tp++)
			if (threads.begin()->second->check(tp->second, 0, true, resources)) {
				instances++;
				// !! what about the resources?
				return true;
			}
		return false;
	}

	unsigned int min = 0, max = 0;
	for (ExpThreadSet::const_iterator tp=threads.begin(); tp!=threads.end(); tp++) {
		min += tp->second->min;
		max += tp->second->max;
	}
	if (p->children.size() < min || p->children.size() > max) {
		if (debug_failed_matches) {
			printf("Recognizer %s\n", name.c_str());
			printf("  false, R.threads={%d,%d} P.threads=%d\n", min, max, p->children.size());
		}
		return false;
	}

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

	if (!root->check(p->children.find(p->root_thread)->second, 0, false,
			resources)) {
		if (debug_failed_matches) {
			printf("Recognizer %s\n", name.c_str());
			printf("  P.root (tid=%d) does not match R.root\n", p->root_thread);
			debug_failed_matches_individually = true;
			(void)root->check(p->children.find(p->root_thread)->second, 0, false, resources);
			debug_failed_matches_individually = false;
		}
		return false;
	}

	for (std::map<int,PathEventList>::const_iterator ptp=p->children.begin(); ptp!=p->children.end(); ptp++) {
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
				printf("Recognizer %s\n", name.c_str());
				printf("  P.thread[%d] did not match anything.  Let's see why...\n", ptp->first);
				debug_failed_matches_individually = true;
				for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++) {
					if (etp->second == root) continue;
					printf("    R.thread %s...\n", etp->first.c_str());
					if (etp->second->check(ptp->second, 0, false, resources)) continue;
				}
				debug_failed_matches_individually = false;
			}
			return false;
		}
	}

	for (ExpThreadSet::const_iterator etp=threads.begin(); etp!=threads.end(); etp++) {
		if (etp->second->count > etp->second->max) {
			if (debug_failed_matches)
				printf("Rejected because there were too many of thread \"%s\"\n", etp->first.c_str());
			return false;
		}
		if (etp->second->count < etp->second->min) {
			if (debug_failed_matches)
				printf("Rejected because there weren't enough of thread \"%s\"\n", etp->first.c_str());
			return false;
		}
	}

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
	latency.add(p->ts_end - p->ts_start);
	size.add(p->size);
	messages.add(p->messages);
	depth.add(p->depth);
	hosts.add(p->hosts);
	threadcount.add(p->children.size());
	return true;
}

int Recognizer::check(const PathEventList &test, const ExpEventList &list,
		int ofs, bool *resources) {
	unsigned int my_ofs = ofs;
	unsigned int i;
	for (i=0; i<list.size(); i++) {
		int res = list[i]->check(test, my_ofs, resources);
		if (res == -1) { return -1; }
		my_ofs += res;
		assert(my_ofs <= test.size());
	}
	return my_ofs - ofs;
}

static void add_statement(const Node *node, ExpEventList *where) {
	switch (node->type()) {
		case NODE_LIST:
			add_statements((ListNode*)node, where);
			break;
		case NODE_OPERATOR:{
				OperatorNode *onode = (OperatorNode*)node;
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
					case '=':
						fprintf(stderr, "op %s not implemented yet\n", get_op_name(onode->op));
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
		assert(((OperatorNode*)(*limit_list)[i])->op == LIMIT);
		limits->push_back(new Limit((OperatorNode*)(*limit_list)[i]));
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
