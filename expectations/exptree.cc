#include <assert.h>
#include <map>
#include "common.h"
#include "exptree.h"
#include "expect.tab.hh"

static void move_send_and_receive(ExpEventList &where);
static void add_statements(const ListNode *list_node, ExpEventList *where,
		LimitList *limits, ExpThreadSet &threads);

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

RegexMatch::RegexMatch(const std::string &_data, bool _negate) : Match(_negate) {}
RegexMatch::~RegexMatch(void) {}
bool RegexMatch::check(const std::string &text) const { return true; }
bool StringMatch::check(const std::string &text) const { bool ret = data == text; return negate ? !ret : ret; }
bool VarMatch::check(const std::string &text) const { return true; }

static const char *metric_name[] = {
	"REAL_TIME", "UTIME", "STIME", "CPU_TIME", "MAJOR_FAULTS", "MINOR_FAULTS",
	"VOL_CS", "INVOL_CS", "LATENCY", "SIZE", NULL
};

Limit::Metric Limit::metric_by_name(const std::string &name) {
	for (int i=0; metric_name[i]; i++)
		if (name == metric_name[i])
			return (Limit::Metric)i;
	fprintf(stderr, "Invalid metric \"%s\"\n", name.c_str());
	abort();
}

Limit::Limit(const OperatorNode *onode) : metric(LAST) {
	assert(onode->op == LIMIT);
	assert(onode->nops() == 2);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);
	metric = metric_by_name(((IdentifierNode*)onode->operands[0])->sym->name);

	assert(onode->operands[1]->type() == NODE_OPERATOR);
	OperatorNode *range = (OperatorNode*)onode->operands[1];
	assert(range->op == RANGE);
	assert(range->nops() == 2);

	assert(range->operands[0]->type() == NODE_UNITS);
	min = ((UnitsNode*)range->operands[0])->amt;
	assert(min >= 0);
	if (range->operands[1]) {
		assert(range->operands[1]->type() == NODE_UNITS);
		max = ((UnitsNode*)range->operands[1])->amt;
		assert(max >= min);
		assert(((UnitsNode*)range->operands[0])->amt >= ((UnitsNode*)range->operands[0])->amt);
	}
	else
		max = -1;
}

void Limit::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<limit metric=\"%s\" min=\"%f\" max=\"%f\" />\n",
		depth*2, "", metric_name[(int)metric], min, max);
}

bool Limit::check(const PathTask *test) const {
	switch (metric) {
		case REAL_TIME:      return check(test->ts_end - test->ts_start);
		case UTIME:          return check(test->utime);
		case STIME:          return check(test->stime);
		case CPU_TIME:       return check(test->utime + test->stime);
		case MAJOR_FAULTS:   return check(test->major_fault);
		case MINOR_FAULTS:   return check(test->minor_fault);
		case VOL_CS:         return check(test->vol_cs);
		case INVOL_CS:       return check(test->invol_cs);
		case LATENCY:
		case SIZE:
		default:
			fprintf(stderr, "Metric %d invalid or unknown when checking Task\n",
				metric);
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
		case MAJOR_FAULTS:
		case MINOR_FAULTS:
		case VOL_CS:
		case INVOL_CS:
		default:
			fprintf(stderr, "Metric %d invalid or unknown when checking Message\n",
				metric);
			exit(1);
	}
}

bool Limit::check(const Path *test) const {
	switch (metric) {
		case REAL_TIME:      return check(test->ts_end - test->ts_start);
		case UTIME:          return check(test->utime);
		case STIME:          return check(test->stime);
		case CPU_TIME:       return check(test->utime + test->stime);
		case MAJOR_FAULTS:   return check(test->major_fault);
		case MINOR_FAULTS:   return check(test->minor_fault);
		case VOL_CS:         return check(test->vol_cs);
		case INVOL_CS:       return check(test->invol_cs);
		case SIZE:           return check(test->size);
		case LATENCY:
		default:
			fprintf(stderr, "Metric %d unknown when checking Path\n", metric);
			exit(1);
	}
}

ExpTask::ExpTask(const OperatorNode *onode, ExpThreadSet &threads) {
	assert(onode->op == TASK);
	assert(onode->nops() == 3);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *task_decl = (OperatorNode*)onode->operands[0];
	assert(task_decl->op == TASK);
	assert(task_decl->nops() == 2);

	name = Match::create((StringNode*)task_decl->operands[0], false);
	host = Match::create((StringNode*)task_decl->operands[1], false);

	ListNode *limit_list = (ListNode*)onode->operands[1];
	for (unsigned int i=0; i<limit_list->size(); i++)
		limits.push_back(new Limit((OperatorNode*)(*limit_list)[i]));

	if (onode->operands[2]) {
		assert(onode->operands[2]->type() == NODE_LIST);
		add_statements((ListNode*)onode->operands[2],
			&children, NULL, threads);
	}
}

ExpTask::~ExpTask(void) {
	delete name;
	delete host;
	unsigned int i;
	for (i=0; i<children.size(); i++) delete children[i];
	for (i=0; i<limits.size(); i++) delete limits[i];
}

void ExpTask::print(FILE *fp, int depth) const {
	bool empty = children.size() == 0 && limits.size() == 0;

	fprintf(fp, "%*s<task name=\"%s\" host=\"%s\"%s",
		depth*2, "", name->to_string(), host->to_string(), empty ? " />\n" : ">\n");
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
	if (ofs == test.size()) return -1;  // we need at least one
	if (test[ofs]->type() != PEV_TASK) return -1;
	PathTask *pt = (PathTask*)test[ofs];
	if (!name->check(pt->name)) return -1;  // name
	// !! host
	if (resources)
		for (unsigned int i=0; i<limits.size(); i++)
			if (!limits[i]->check(pt)) {
				*resources = false;
				break;
			}

	if (Recognizer::check(pt->children, children, 0, resources)
			!= (int)pt->children.size()) return -1;  // children
	return 1;
}

ExpNotice::ExpNotice(const OperatorNode *onode) {
	assert(onode->op == NOTICE);
	assert(onode->nops() == 2);

	name = Match::create((StringNode*)onode->operands[0], false);
	host = Match::create((StringNode*)onode->operands[1], false);
}

void ExpNotice::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" host=\"%s\" />\n",
		depth*2, "", name->to_string(), host->to_string());
}

int ExpNotice::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) return -1;  // we need at least one
	if (test[ofs]->type() != PEV_NOTICE) return -1;
	PathNotice *pn = (PathNotice*)test[ofs];
	if (!name->check(pn->name)) return -1;  // name
	// !! host
	return 1;
}

ExpMessageSend::ExpMessageSend(const OperatorNode *onode, int _id) : id(_id) {
	assert(onode->op == MESSAGE);
	assert(onode->nops() == 1);
	if (onode->operands[0]) {
		assert(onode->operands[0]->type() == NODE_LIST);
		ListNode *limit_list = (ListNode*)onode->operands[0];
		for (unsigned int i=0; i<limit_list->size(); i++)
			limits.push_back(new Limit((OperatorNode*)(*limit_list)[i]));
	}
}

ExpMessageSend::~ExpMessageSend(void) {
	for (unsigned int i=0; i<limits.size(); i++)
		delete limits[i];
}

void ExpMessageSend::print(FILE *fp, int depth) const {
	bool empty = limits.size() == 0;
	fprintf(fp, "%*s<message_send id=\"%d\"%s", depth*2, "", id, empty ? " />\n" : ">\n");
	if (!empty) {
		for (unsigned int i=0; i<limits.size(); i++)
			limits[i]->print(fp, depth+1);
		fprintf(fp, "%*s</message_send>\n", depth*2, "");
	}
}

int ExpMessageSend::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) return -1;  // we need at least one
	if (test[ofs]->type() != PEV_MESSAGE_SEND) return -1;
	PathMessageSend *pms = (PathMessageSend*)test[ofs];
	if (resources)
		for (unsigned int i=0; i<limits.size(); i++)
			if (!limits[i]->check(pms)) {
				*resources = false;
				break;
			}

	return 1;
}

ExpMessageRecv::ExpMessageRecv(const OperatorNode *onode, int _id) : id(_id) {
	assert(onode->op == MESSAGE);
}

void ExpMessageRecv::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message_recv id=\"%d\" />\n", depth*2, "", id);
}

int ExpMessageRecv::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	if (ofs == test.size()) return -1;  // we need at least one
	if (test[ofs]->type() != PEV_MESSAGE_RECV) return -1;

	return 1;
}

ExpRepeat::ExpRepeat(const OperatorNode *onode, ExpThreadSet &threads) {
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

	assert(onode->operands[1]->type() == NODE_LIST);
	add_statements((ListNode*)onode->operands[1], &children, NULL, threads);
}

ExpRepeat::~ExpRepeat(void) {
	for (unsigned int i=0; i<children.size(); i++)
		delete children[i];
}

void ExpRepeat::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<repeat min=%d max=%d>\n", depth*2, "", min, max);
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, depth+1);
	fprintf(fp, "%*s</repeat>\n", depth*2, "");
}

int ExpRepeat::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	int count, my_ofs=ofs;
	for (count=0; count<=max; count++) {
		int res = Recognizer::check(test, children, my_ofs, resources);
		if (res == -1) break;
		my_ofs += res;
	}
	if (count >= min) return my_ofs - ofs;
	return -1;
}

ExpXor::ExpXor(const OperatorNode *onode, ExpThreadSet &threads) {
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
		add_statements((ListNode*)branch->operands[1], &branches[j], NULL, threads);
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
	return -1;
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

int ExpCall::check(const PathEventList &test, unsigned int ofs,
		bool *resources) const {
	Recognizer *r = recognizers[target];
	if (!r) {
		fprintf(stderr, "call(%s): recognizer not found\n", target.c_str());
		exit(1);
	}
	// !! broken -- don't force it to threads[0]
	return r->check(test, r->threads[0], ofs, resources);
	// !! if the called recognizer sets whole-path limits, check those
}

Recognizer::Recognizer(const IdentifierNode *ident, const ListNode *statements, bool _complete, bool _validating)
		: complete(_complete), validating(_validating), instances(0), unique(0) {
	assert(ident->type() == NODE_IDENTIFIER);
	name = ident->sym->name;

	assert(statements->type() == NODE_LIST);
	// Reserve enough space that the vector won't have to expand itself.
	// !! Ugly, ugly.  But it keeps the iterators from getting invalidated.
	threads.reserve(1000);
	threads.push_back(ExpEventList());
	add_statements(statements, &threads[0], &limits, threads);
	for (unsigned int i=0; i<threads.size(); i++)
		move_send_and_receive(threads[i]);
}

Recognizer::~Recognizer(void) {
	unsigned int i;
	for (unsigned int t=0; t<threads.size(); t++)
		for (i=0; i<threads[t].size(); i++) delete threads[t][i];
	for (i=0; i<limits.size(); i++) delete limits[i];
}

void Recognizer::print(FILE *fp) const {
	fprintf(fp, "<recognizer name=\"%s\" validator=\"%s\" complete=\"%s\">\n",
		name.c_str(), validating?"true":"false", complete?"true":"false");
	unsigned int i, t;
	for (i=0; i<limits.size(); i++)
		limits[i]->print(fp, 1);
	for (t=0; t<threads.size(); t++) {
		printf("  <!-- begin thread %d -->\n", t);
		for (i=0; i<threads[t].size(); i++) {
			const ExpEventList *list = &threads[t];
			const ExpEvent *ev = (*list)[i];
			ev->print(fp, 1);
		}
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

bool Recognizer::check(const Path *p, bool *resources) {
	if (!complete) {
		for (std::map<int,PathEventList>::const_iterator tp=p->children.begin(); tp!=p->children.end(); tp++)
			if (check_fragment(tp->second, threads[0], 0, resources)) {
				instances++;
				// !! what about the rest of the resources?
				return true;
			}
		return false;
	}

	printf("Recognizer %s\n", name.c_str());
	if (threads.size() != p->children.size()) {
		printf(" -> false, R.threads=%d P.threads=%d\n", threads.size(), p->children.size());
		return false;
	}

	if (resources)
		for (unsigned int i=0; i<limits.size(); i++)
			if (!limits[i]->check(p)) {
				*resources = false;
				break;
			}

	std::vector<int> thread_map(threads.size(), -1);
	unsigned int i;
	assert(p->root_thread != -1);
	thread_map[0] = p->root_thread;
	// make the rest of the thread map by transitively following messages
	// this part MAY FAIL and cause the function to return false...
	
	// !! this ain't it
	i = 1;
	for (std::map<int,PathEventList>::const_iterator tp=p->children.begin(); tp!=p->children.end(); tp++) {
		if (tp->first == p->root_thread) continue;
		thread_map[i++] = tp->first;
	}
	
	for (i=0; i<thread_map.size(); i++) {
		printf("thread_map[%d] = %d\n", i, thread_map[i]);
		if (check(p->children.find(thread_map[i])->second, threads[i], 0, resources) != (int)threads[i].size()) return false;
	}

	instances++;
	// unique++ if path or hosts different !!
	real_time.add(p->ts_end - p->ts_start);
	utime.add(p->utime);
	stime.add(p->stime);
	cpu_time.add(p->utime + p->stime);
	major_fault.add(p->major_fault);
	minor_fault.add(p->minor_fault);
	vol_cs.add(p->vol_cs);
	invol_cs.add(p->invol_cs);
	size.add(p->size);
	return true;
}

int Recognizer::check(const PathEventList &test, const ExpEventList &list,
		int ofs, bool *resources) {
	unsigned int my_ofs = ofs;
	unsigned int i;
	for (i=0; i<list.size(); i++) {
		int res = list[i]->check(test, my_ofs, resources);
		if (res == -1) return -1;
		my_ofs += res;
		assert(my_ofs <= test.size());
	}
	return my_ofs - ofs;
}

/* Move all send events at the beginning, and receive events at the end,
 * of a thread into adjacent task events, if present.
 * !! This is a giant kludge.  I would like to match either way, not force
 * messages to be children of tasks. */
static void move_send_and_receive(ExpEventList &where) {
	if (where.size() >= 2 && where[0]->type() == EXP_MESSAGE_RECV && where[1]->type() == EXP_TASK) {
		ExpTask *task = (ExpTask*)where[1];
		task->children.insert(task->children.begin(), where[0]);
		where.erase(where.begin());
	}
	if (where.size() >= 2 && where[where.size()-1]->type() == EXP_MESSAGE_SEND && where[where.size()-2]->type() == EXP_TASK) {
		ExpTask *task = (ExpTask*)where[where.size()-2];
		task->children.push_back(where[where.size()-1]);
		where.pop_back();
	}
}

static int msgseq = 0;
static void add_statements(const ListNode *list_node, ExpEventList *where,
		LimitList *limits, ExpThreadSet &threads) {
	unsigned int i;
	assert(where != NULL);
	ExpEventList *local_where = where;
	for (i=0; i<list_node->size(); i++) {
		const Node *node = (*list_node)[i];
		switch (node->type()) {
			case NODE_LIST:
				add_statements((ListNode*)node, local_where, NULL, threads);
				break;
			case NODE_OPERATOR:{
					OperatorNode *onode = (OperatorNode*)node;
					switch (onode->op) {
						case THREAD: break;  //!!
						case TASK:
							local_where->push_back(new ExpTask(onode, threads));
							break;
						case NOTICE:
							local_where->push_back(new ExpNotice(onode));
							break;
						case REPEAT:
							local_where->push_back(new ExpRepeat(onode, threads));
							break;
						case XOR:
							local_where->push_back(new ExpXor(onode, threads));
							break;
						case LIMIT:
							assert(limits);
							limits->push_back(new Limit(onode));
							break;
						case CALL:
							local_where->push_back(new ExpCall(onode));
							break;
						case MESSAGE:
							local_where->push_back(new ExpMessageSend(onode, msgseq));
							if (i == list_node->size() - 1)
								local_where = where;
							else {
								threads.push_back(ExpEventList());
								local_where = &threads[threads.size()-1];
							}
							local_where->push_back(new ExpMessageRecv(onode, msgseq++));
							break;
						case REVERSE:
						case SPLIT:
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
}
