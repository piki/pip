/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include "aggregates.h"
#include "exptree.h"
#include "expect.tab.hh"

Aggregate::Aggregate(Node *_node) {
	assert(_node->type() == NODE_OPERATOR);
	OperatorNode *onode = dynamic_cast<OperatorNode*>(_node);
	assert(onode->nops() == 1);
	assert(onode->operands[0]->type() == NODE_OPERATOR);
	node = onode->operands[0];
}

void Aggregate::print(FILE *fp) const {
	print_assert(fp, node);
}

void Aggregate::print_tree(FILE *fp) const {
	fprintf(fp, "<assert>\n");
	::print_tree(node, 1);
	fprintf(fp, "</assert>\n");
}

bool Aggregate::check(void) const {
	return eval_bool(node) != 0;
}

static const RecognizerBase *get_recognizer(const std::string &name) {
	const RecognizerBase *r = recognizers_by_name[name];
	if (!r) {
		fprintf(stderr, "Recognizer not found: %s\n", name.c_str());
		abort();
	}
	return r;
}

static const Counter &get_metric(const RecognizerBase *r, const std::string &name) {
	Limit::Metric metric = Limit::metric_by_name(name);
	switch (metric) {
		case Limit::REAL_TIME:     return r->real_time;
		case Limit::UTIME:         return r->utime;
		case Limit::STIME:         return r->stime;
		case Limit::CPU_TIME:      return r->cpu_time;
		case Limit::BUSY_TIME:     return r->busy_time;
		case Limit::MAJOR_FAULTS:  return r->minor_fault;
		case Limit::MINOR_FAULTS:  return r->major_fault;
		case Limit::VOL_CS:        return r->vol_cs;
		case Limit::INVOL_CS:      return r->invol_cs;
		case Limit::SIZE:          return r->size;
		case Limit::THREADS:       return r->threadcount;
		case Limit::MESSAGES:      return r->messages;
		case Limit::DEPTH:         return r->depth;
		case Limit::HOSTS:         return r->hosts;
		case Limit::LATENCY:       return r->latency;
		default:
			fprintf(stderr, "unknown metric: %d\n", metric);
			abort();
	}
}

float Aggregate::eval_float(const Node *n) const {
	const OperatorNode *onode;
	const RecognizerBase *r;
	switch (n->type()) {
		case NODE_INT:         return dynamic_cast<const IntNode*>(n)->value;
		case NODE_FLOAT:       return dynamic_cast<const FloatNode*>(n)->value;
		case NODE_UNITS:       return dynamic_cast<const UnitsNode*>(n)->amt;
		case NODE_OPERATOR:
			onode = dynamic_cast<const OperatorNode*>(n);
			switch (onode->op) {
				case '/':
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) / eval_float(onode->operands[1]);
				case '+':
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) + eval_float(onode->operands[1]);
				case '-':
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) - eval_float(onode->operands[1]);
				case '*':
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) * eval_float(onode->operands[1]);
				case LOG:
					assert(onode->nops() == 1);
					return log(eval_float(onode->operands[0]))/log(10);
				case LOGN:
					assert(onode->nops() == 1);
					return log(eval_float(onode->operands[0]));
				case EXP:
					assert(onode->nops() == 1);
					return exp(eval_float(onode->operands[0]));
				case SQRT:
					assert(onode->nops() == 1);
					return sqrt(eval_float(onode->operands[0]));
				case F_POW:
					assert(onode->nops() == 2);
					return pow(eval_float(onode->operands[0]), eval_float(onode->operands[1]));
				case INSTANCES:
					assert(onode->nops() == 1);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					r = get_recognizer(dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name);
					return r->instances;
				case AVERAGE:
					assert(onode->nops() == 2);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					r = get_recognizer(dynamic_cast<IdentifierNode*>(onode->operands[1])->sym->name);
					return get_metric(r, dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name).avg();
				case STDDEV:
					assert(onode->nops() == 2);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					assert(onode->operands[1]->type() == NODE_IDENTIFIER);
					r = get_recognizer(dynamic_cast<IdentifierNode*>(onode->operands[1])->sym->name);
					return get_metric(r, dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name).stddev();
				case F_MAX:
					assert(onode->nops() == 2);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					assert(onode->operands[1]->type() == NODE_IDENTIFIER);
					r = get_recognizer(dynamic_cast<IdentifierNode*>(onode->operands[1])->sym->name);
					return get_metric(r, dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name).max();
				case F_MIN:
					assert(onode->nops() == 2);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					assert(onode->operands[1]->type() == NODE_IDENTIFIER);
					r = get_recognizer(dynamic_cast<IdentifierNode*>(onode->operands[1])->sym->name);
					return get_metric(r, dynamic_cast<IdentifierNode*>(onode->operands[0])->sym->name).min();
				case UNIQUE:
					assert(onode->nops() == 1);
					assert(onode->operands[0]->type() == NODE_IDENTIFIER);
					// fall through to "not implemented"
				default:
					fprintf(stderr, "Operator %s (%d) not implemented for eval_float\n", get_op_name(onode->op), onode->op);
					abort();
			}
		default:
			fprintf(stderr, "Node type %d not implemented for eval_float\n", n->type());
			abort();
	}
}

bool Aggregate::eval_bool(const Node *n) const {
	const OperatorNode *onode;
	switch (n->type()) {
		case NODE_OPERATOR:
			onode = dynamic_cast<const OperatorNode*>(n);
			switch (onode->op) {
				case EQ:
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) == eval_float(onode->operands[1]);
				case '<':
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) < eval_float(onode->operands[1]);
				case '>':
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) > eval_float(onode->operands[1]);
				case LE:
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) <= eval_float(onode->operands[1]);
				case GE:
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) >= eval_float(onode->operands[1]);
				case NE:
					assert(onode->nops() == 2);
					return eval_float(onode->operands[0]) != eval_float(onode->operands[1]);
				case B_AND:
					assert(onode->nops() == 2);
					return eval_bool(onode->operands[0]) && eval_bool(onode->operands[1]);
				case B_OR:
					assert(onode->nops() == 2);
					return eval_bool(onode->operands[0]) || eval_bool(onode->operands[1]);
				case IMPLIES:
					assert(onode->nops() == 2);
					return !eval_bool(onode->operands[0]) || eval_bool(onode->operands[1]);
				case '!':
					assert(onode->nops() == 1);
					return !eval_bool(onode->operands[0]);
				case IN:{
					assert(onode->nops() == 2);
					assert(onode->operands[1]->type() == NODE_OPERATOR);
					OperatorNode *right = dynamic_cast<OperatorNode*>(onode->operands[1]);
					assert(right->op == RANGE);
					assert(right->nops() == 2);
					float min = eval_float(right->operands[0]);
					float max = eval_float(right->operands[1]);
					float test = eval_float(onode->operands[0]);
					return min <= test && test <= max;
				}
				default:
					fprintf(stderr, "Operator %s (%d) not implemented for eval_bool\n", get_op_name(onode->op), onode->op);
					abort();
			}
		default:
			fprintf(stderr, "Node type %d not implemented for eval_bool\n", n->type());
			abort();
	}
}

void Counter::add(float n) {
	sum += n;
	sumsq += n*n;
	if (count == 0 || n < _min) _min = n;
	if (count == 0 || n > _max) _max = n;
	count++;
}
