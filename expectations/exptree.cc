#include <assert.h>
#include "exptree.h"
#include "expect.tab.hh"

Match *Match::create(StringNode *node) {
	if (node->type() == NODE_STRING)
		return new StringMatch(((StringNode*)node)->s);
	else if (node->type() == NODE_REGEX)
		return new RegexMatch(((StringNode*)node)->s);
	else if (node->type() == NODE_IDENTIFIER)
		return new VarMatch(((IdentifierNode*)node)->sym);
	else if (node->type() == NODE_OPERATOR) {
		OperatorNode *onode = (OperatorNode*)node;
		assert(onode->op == '=');
		assert(onode->noperands == 2);
		fprintf(stderr, "string_expr assignment not yet supported\n");
		return create((StringNode*)onode->operands[1]);
	}
	else {
		fprintf(stderr, "invalid type where string_expr expected: %d\n",
			node->type());
		assert(!"not reached");
		return NULL;
	}
}

RegexMatch::RegexMatch(const std::string &_data) {}
RegexMatch::~RegexMatch(void) {}
bool RegexMatch::check(const std::string &text) const { return true; }
bool StringMatch::check(const std::string &text) const { return data == text; }
bool VarMatch::check(const std::string &text) const { return true; }

ExpTask::ExpTask(const OperatorNode *onode) {
	assert(onode->op == TASK);
	assert(onode->noperands == 3);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *task_decl = (OperatorNode*)onode->operands[0];
	assert(task_decl->op == TASK);
	assert(task_decl->noperands == 2);

	name = Match::create((StringNode*)task_decl->operands[0]);
	host = Match::create((StringNode*)task_decl->operands[1]);

	if (((ListNode*)onode->operands[1])->size() != 0)
		fprintf(stderr, "ignoring %d limits\n",
			((ListNode*)onode->operands[1])->size());
}

ExpTask::~ExpTask(void) {
	delete name;
	delete host;
	for (unsigned int i=0; i<children.size(); i++) delete children[i];
}

void ExpTask::print(FILE *fp, int depth) const {
	bool empty = children.size() == 0;

	fprintf(fp, "%*s<task name=\"%s\" host=\"%s\"%s",
		depth*2, "", name->to_string(), host->to_string(), empty ? " />\n" : ">\n");
	if (!empty) {
		for (unsigned int i=0; i<children.size(); i++)
			children[i]->print(fp, depth+1);
		fprintf(fp, "%*s</task>\n", depth*2, "");
	}
}

int ExpTask::check(const PathEventList &test, unsigned int ofs) const {
	if (ofs == test.size()) return -1;  // we need at least one
	if (test[ofs]->type() != PEV_TASK) return -1;
	PathTask *pt = (PathTask*)test[ofs];
	if (!name->check(pt->name)) return -1;  // name
	// !! host
	if (Recognizer::check(pt->children, children, 0) != (int)pt->children.size()) return -1;  // children
	return 1;
}

ExpNotice::ExpNotice(const OperatorNode *onode) {
	assert(onode->op == NOTICE);
	assert(onode->noperands == 2);

	name = Match::create((StringNode*)onode->operands[0]);
	host = Match::create((StringNode*)onode->operands[1]);
}

void ExpNotice::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<notice name=\"%s\" host=\"%s\" />\n",
		depth*2, "", name->to_string(), host->to_string());
}

int ExpNotice::check(const PathEventList &test, unsigned int ofs) const {
	if (ofs == test.size()) return -1;  // we need at least one
	if (test[ofs]->type() != PEV_NOTICE) return -1;
	PathNotice *pn = (PathNotice*)test[ofs];
	if (!name->check(pn->name)) return -1;  // name
	// !! host
	return 1;
}

void ExpMessage::print(FILE *fp, int depth) const {
	fprintf(fp, "%*s<message />\n", depth*2, "");
}

int ExpMessage::check(const PathEventList &test, unsigned int ofs) const {
	return 1;
}

ExpRepeat::ExpRepeat(const OperatorNode *onode) {
	assert(onode->op == REPEAT);
	assert(onode->noperands == 2);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	OperatorNode *range = (OperatorNode*)onode->operands[0];
	assert(range->op == RANGE);
	assert(range->noperands == 2);
	assert(range->operands[0]->type() == NODE_INT);
	assert(range->operands[1]->type() == NODE_INT);

	min = ((IntNode*)range->operands[0])->value;
	max = ((IntNode*)range->operands[1])->value;
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

int ExpRepeat::check(const PathEventList &test, unsigned int ofs) const {
	int count, my_ofs=ofs;
	for (count=0; count<=max; count++) {
		int res = Recognizer::check(test, children, my_ofs);
		if (res == -1) break;
		my_ofs += res;
	}
	if (count >= min) return my_ofs - ofs;
	return -1;
}

ExpXor::ExpXor(const OperatorNode *onode) {
	assert(onode->op == XOR);
	assert(onode->noperands == 1);
	assert(onode->operands[0]->type() == NODE_LIST);
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

int ExpXor::check(const PathEventList &test, unsigned int ofs) const {
	for (unsigned int i=0; i<branches.size(); i++) {
		int res = Recognizer::check(test, branches[i], ofs);
		if (res != -1) return res;  // !! greedy, continuation needed
	}
	return -1;
}

Recognizer::Recognizer(const Node *node) {
	assert(node->type() == NODE_OPERATOR);
	const OperatorNode *onode = (OperatorNode*)node;
	assert(onode->op == EXPECTATION || onode->op == FRAGMENT);
	complete = (onode->op == EXPECTATION);
	assert(onode->noperands == 2);
	assert(onode->operands[0]->type() == NODE_IDENTIFIER);
	assert(onode->operands[1]->type() == NODE_LIST);
	name = ((IdentifierNode*)onode->operands[0])->sym;
	const ListNode *statements = (ListNode*)onode->operands[1];
	add_statements(statements, &children);
}

Recognizer::~Recognizer(void) {
	for (unsigned int i=0; i<children.size(); i++) delete children[i];
}

void Recognizer::add_statements(const ListNode *list, ExpEventList *where) {
	unsigned int i;
	assert(where != NULL);
	for (i=0; i<list->size(); i++) {
		const Node *node = (*list)[i];
		switch (node->type()) {
			case NODE_LIST:
				add_statements((ListNode*)node, where);
				break;
			case NODE_OPERATOR:{
					OperatorNode *onode = (OperatorNode*)node;
					switch (onode->op) {
						case TASK:{
								ExpTask *new_task = new ExpTask(onode);
								if (onode->operands[2]) {
									assert(onode->operands[2]->type() == NODE_LIST);
									add_statements((ListNode*)onode->operands[2],
										&new_task->children);
								}
								where->push_back(new_task);
							}
							break;
						case NOTICE:
							where->push_back(new ExpNotice(onode));
							break;
						case REPEAT:{
								ExpRepeat *new_repeat = new ExpRepeat(onode);
								assert(onode->operands[1]->type() == NODE_LIST);
								add_statements((ListNode*)onode->operands[1],
									&new_repeat->children);
								where->push_back(new_repeat);
							}
							break;
						case XOR:{
								ExpXor *new_xor = new ExpXor(onode);
								ListNode *lnode = (ListNode*)onode->operands[0];
								for (unsigned int j=0; j<lnode->size(); j++) {
									assert((*lnode)[j]->type() == NODE_OPERATOR);
									OperatorNode *branch = (OperatorNode*)(*lnode)[j];
									assert(branch->op == BRANCH);
									assert(branch->noperands == 1);
									assert(branch->operands[0]->type() == NODE_LIST);
									new_xor->branches.push_back(ExpEventList());
									add_statements((ListNode*)branch->operands[0],
										&new_xor->branches[j]);
								}
								where->push_back(new_xor);
							}
							break;
						case MESSAGE:
						case REVERSE:
						case CALL:
							fprintf(stderr, "op %s not implemented yet\n", get_op_name(onode->op));
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

void Recognizer::print(FILE *fp) const {
	fprintf(fp, "<recognizer name=\"%s\">\n", name->name.c_str());
	for (unsigned int i=0; i<children.size(); i++)
		children[i]->print(fp, 1);
	fprintf(fp, "</recognizer>\n");
}

bool Recognizer::check(const Path &p) const {
	return check(p.children, children, 0) == (int)p.children.size();
}

int Recognizer::check(const PathEventList &test, const ExpEventList &list, int ofs) {
	unsigned int my_ofs = ofs;
	unsigned int i;
	for (i=0; i<list.size(); i++) {
		int res = list[i]->check(test, my_ofs);
		if (res == -1) return -1;
		my_ofs += res;
		assert(my_ofs <= test.size());
	}
	return my_ofs - ofs;
}
