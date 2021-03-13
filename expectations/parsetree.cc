/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <map>
#include <string>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "parsetree.h"
#include "expect.tab.hh"
#include "exptree.h"

static std::map<std::string, Symbol*> symbol_table;

Symbol::Symbol(char *_name, SymbolType _type, bool _global)
		: type(_type), name(_name), global(_global) {
	memset(_name, 'X', strlen(_name));  // help check for use-after-free
	free(_name);  // lex file strdups it, we free it.  ugly.
}

extern int yylno;
extern char *yyfilename;
extern bool yy_success;

Symbol *Symbol::create(char *name, SymbolType type, bool global, bool excl) {
	assert(type != SYM_ANY);
	Symbol *sym = symbol_table[name];
	if (sym) {
		if (excl) {
			fprintf(stderr, "%s:%d: symbol \"%s\" redefined\n",
				yyfilename, yylno, name);
			yy_success = this_path_ok = false;
		}
		else if (sym->type != type) {
			fprintf(stderr, "%s:%d: symbol \"%s\" type mismatch\n",
				yyfilename, yylno, name);
			yy_success = this_path_ok = false;
		}
	}
	else {
		sym = new Symbol(name, type, global);
		symbol_table[sym->name] = sym;
	}
	return sym;
}

Symbol *Symbol::find(const char *name, SymbolType type) {
	Symbol *sym = symbol_table[name];
	if (!sym) {
		fprintf(stderr, "%s:%d: undefined symbol: %s\n", yyfilename, yylno, name);
		yy_success = this_path_ok = false;
	}
	else if (type != SYM_ANY && sym->type != type) {
		fprintf(stderr, "%s:%d: type mismatch: %s\n", yyfilename, yylno, name);
		yy_success = this_path_ok = false;
	}
	return sym;
}

OperatorNode::OperatorNode(int which, int argc, ...) {
	op = which;
	va_list args;
	va_start(args, argc);
	for (int i=0; i<argc; i++)
		operands.push_back(va_arg(args, Node*));
	va_end(args);
}

OperatorNode::~OperatorNode(void) {
	for (unsigned int i=0; i<operands.size(); i++)
		if (operands[i]) delete operands[i];
}

int OperatorNode::int_value(void) const {
	switch (op) {
		case '+':
			assert(nops() == 2);
			return operands[0]->int_value() + operands[1]->int_value();
		case '-':
			assert(nops() == 2);
			return operands[0]->int_value() - operands[1]->int_value();
		case '*':
			assert(nops() == 2);
			return operands[0]->int_value() * operands[1]->int_value();
	}
	fprintf(stderr, "OperatorNode::int_value: unknown op '%c' (%d)\n", op, op);
	abort();
}

static const struct {
	const char *name;
	UnitType unit;
} unit_map[] = {
	{ "h", UNIT_HOUR },
	{ "m", UNIT_MIN },
	{ "s", UNIT_SEC },
	{ "ms", UNIT_MSEC },
	{ "us", UNIT_USEC },
	{ "ns", UNIT_NSEC },
	{ "b", UNIT_BYTE },
	{ "kb", UNIT_KB },
	{ "mb", UNIT_MB },
	{ "gb", UNIT_GB },
	{ "tb", UNIT_TB },
	{ NULL, UNIT_NONE }
};

static const char *get_unit_name(UnitType unit) {
	for (int i=0; unit_map[i].name; i++)
		if (unit == unit_map[i].unit) return unit_map[i].name;
	return "!UNKNOWN!";
}

static UnitType get_unit_by_name(const char *sym) {
	for (int i=0; unit_map[i].name; i++)
		if (!strcasecmp(sym, unit_map[i].name))
			return unit_map[i].unit;
	fprintf(stderr, "%s:%d: invalid unit: %s\n", yyfilename, yylno, sym);
	yy_success = this_path_ok = false;
	return UNIT_NONE;
}

const char *UnitsNode::name(void) const {
	return get_unit_name(unit);
}

UnitsNode::UnitsNode(float _amt, const char *_name) : amt(_amt) {
	if (!_name) {
		unit = UNIT_NONE;
		return;
	}

	unit = get_unit_by_name(_name);
	switch (unit) {
		case UNIT_HOUR: amt *= 3600.0*1000000; unit = UNIT_USEC; break;
		case UNIT_MIN:  amt *= 60*1000000; unit = UNIT_USEC; break;
		case UNIT_SEC:  amt *= 1000000; unit = UNIT_USEC; break;
		case UNIT_MSEC: amt *= 1000; unit = UNIT_USEC; break;
		case UNIT_USEC: break;
		case UNIT_NSEC: amt /= 1000; unit = UNIT_USEC; break;
		case UNIT_BYTE: break;
		case UNIT_KB:   amt *= 1024; unit = UNIT_BYTE; break;
		case UNIT_MB:   amt *= 1048576; unit = UNIT_BYTE; break;
		case UNIT_GB:   amt *= 1073741824; unit = UNIT_BYTE; break;
		case UNIT_TB:   amt *= 1099511627776.0; unit = UNIT_BYTE; break;
		default:
			fprintf(stderr, "Invalid unit: %d\n", (int)unit);
			abort();
	}
}

ListNode::~ListNode(void) {
	for (unsigned int i=0; i<data.size(); i++)
		delete data[i];
}

static struct {
	int op;
	const char *name;
} opmap[] = {
	{ VALIDATOR, "VALIDATOR" },
	{ INVALIDATOR, "INVALIDATOR" },
	{ RECOGNIZER, "RECOGNIZER" },
	{ FRAGMENT, "FRAGMENT" },
	{ LEVEL, "LEVEL" },
	{ REPEAT, "REPEAT" },
	{ BRANCH, "BRANCH" },
	{ XOR, "XOR" },
	{ MAYBE, "MAYBE" },
	{ CALL, "CALL" },
	{ FUTURE, "FUTURE" },
	{ DONE, "DONE" },
	{ BETWEEN, "BETWEEN" },
	{ AND, "AND" },
	{ ASSERT, "ASSERT" },
	{ INSTANCES, "INSTANCES" },
	{ UNIQUE, "UNIQUE" },
	{ DURING, "DURING" },
	{ ANY, "ANY" },
	{ AVERAGE, "AVERAGE" },
	{ STDDEV, "STDDEV" },
	{ F_MAX, "F_MAX" },
	{ F_MIN, "F_MIN" },
	{ F_POW, "F_POW" },
	{ SQRT, "SQRT" },
	{ LOG, "LOG" },
	{ LOGN, "LOGN" },
	{ EXP, "EXP" },
	{ GE, "GE" },
	{ LE, "LE" },
	{ EQ, "EQ" },
	{ NE, "NE" },
	{ B_AND, "B_AND" },
	{ B_OR, "B_OR" },
	{ IMPLIES, "IMPLIES" },
	{ IN, "IN" },
	{ SEND, "SEND" },
	{ RECV, "RECV" },
	{ TASK, "TASK" },
	{ THREAD, "THREAD" },
	{ NOTICE, "NOTICE" },
	{ LIMIT, "LIMIT" },
	{ RANGE, "RANGE" },
	{ STRING, "STRING" },
	{ REGEX, "REGEX" },
	{ INTEGER, "INTEGER" },
	{ FLOAT, "FLOAT" },
	{ IDENTIFIER, "IDENTIFIER" },
	{ VARIABLE, "VARIABLE" },
	{ -1, "" },
};

const char *get_op_name(int op) {
	static char buf[2] = " ";
	if (op <= 255) { buf[0] = op; return buf; }
	for (int i=0; opmap[i].op > 0; i++)
		if (op == opmap[i].op)
			return opmap[i].name;
	return "!UNKNOWN!";
}

/* ALMOST print out the input.
 * 1)   Blank lines, spacing, and comments.  Duh.
 * 2)   Parentheses get lost.
 */
void print_assert(FILE *fp, const Node *node) {
	if (!node) { return; }
	switch (node->type()) {
		case NODE_INT:
			fprintf(fp, "%d", dynamic_cast<const IntNode*>(node)->value);
			break;
		case NODE_FLOAT:
			fprintf(fp, "%f", dynamic_cast<const FloatNode*>(node)->value);
			break;
		case NODE_UNITS:
			fprintf(fp, "%.2f%s", dynamic_cast<const UnitsNode*>(node)->amt, dynamic_cast<const UnitsNode*>(node)->name());
			break;
		case NODE_STRING:
			fprintf(fp, "\"%s\"", dynamic_cast<const StringNode*>(node)->s);
			break;
		case NODE_REGEX:
			fprintf(fp, "m/%s/", dynamic_cast<const StringNode*>(node)->s);
			break;
		case NODE_IDENTIFIER:
			fprintf(fp, "%s", dynamic_cast<const IdentifierNode*>(node)->sym->name.c_str());
			break;
		case NODE_LIST:{
				const ListNode *lnode = dynamic_cast<const ListNode*>(node);										 	
				for (unsigned int i=0; i<lnode->size(); i++) {
					print_assert(fp, (*lnode)[i]);
				}
			}
			break;
		case NODE_OPERATOR:{
			const OperatorNode *onode = dynamic_cast<const OperatorNode*>(node);
			switch (onode->op) {
				case RANGE:
					fprintf(fp, "{");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, "...");
					if (onode->operands[1]) print_assert(fp, onode->operands[1]);
					fprintf(fp, "}");
					break;
				case '/':
				case '+':
				case '*':
				case '-':
				case '<':
				case '>':
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " %c ", onode->op);
					print_assert(fp, onode->operands[1]);
					break;
				case B_AND:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " && ");
					print_assert(fp, onode->operands[1]);
					break;
				case B_OR:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " || ");
					print_assert(fp, onode->operands[1]);
					break;
				case IMPLIES:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " -> ");
					print_assert(fp, onode->operands[1]);
					break;
				case IN:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " in ");
					print_assert(fp, onode->operands[1]);
					break;
				case LE:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " <= ");
					print_assert(fp, onode->operands[1]);
					break;
				case GE:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " >= ");
					print_assert(fp, onode->operands[1]);
					break;
				case EQ:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " == ");
					print_assert(fp, onode->operands[1]);
					break;
				case NE:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " != ");
					print_assert(fp, onode->operands[1]);
					break;
				case '!':
					fprintf(fp, "!");
					print_assert(fp, onode->operands[0]);
					break;
				case INSTANCES:
					fprintf(fp, "instances(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ")");
					break;
				case UNIQUE:
					fprintf(fp, "unique(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ")");
					break;
				case F_MAX:
					fprintf(fp, "max(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ", ");
					print_assert(fp, onode->operands[1]);
					fprintf(fp, ")");
					break;
				case LOG:
					fprintf(fp, "log(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ")");
					break;
				case LOGN:
					fprintf(fp, "ln(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ")");
					break;
				case SQRT:
					fprintf(fp, "sqrt(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ")");
					break;
				case F_POW:
					print_assert(fp, onode->operands[0]);
					fprintf(fp, " ** ");
					print_assert(fp, onode->operands[1]);
					break;
				case EXP:
					fprintf(fp, "exp(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ")");
					break;
				case AVERAGE:
					fprintf(fp, "average(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ", ");
					print_assert(fp, onode->operands[1]);
					fprintf(fp, ")");
					break;
				case STDDEV:
					fprintf(fp, "stddev(");
					print_assert(fp, onode->operands[0]);
					fprintf(fp, ", ");
					print_assert(fp, onode->operands[1]);
					fprintf(fp, ")");
					break;
				default:
					fprintf(fp, "\n\n\nunhandled operator: ");
					if (onode->op <= 255)
						fprintf(fp, "%d (%c)\n", onode->op, onode->op);
					else
						fprintf(fp, "%d (%s)\n", onode->op, get_op_name(onode->op));
					abort();
			}
			break;}
		default:
			assert(!"not reached");
	}
}

void print_tree(const Node *node, int depth) {
	unsigned int i;
	printf("%*sNode %p: ", depth*2, "", node);
	if (!node) { printf("NULL\n"); return; }
	switch (node->type()) {
		case NODE_INT:
			printf("int: %d\n", dynamic_cast<const IntNode*>(node)->value);
			break;
		case NODE_FLOAT:
			printf("float: %f\n", dynamic_cast<const FloatNode*>(node)->value);
			break;
		case NODE_UNITS:{
			const UnitsNode *unode = dynamic_cast<const UnitsNode*>(node);
			printf("units: %.2f %s\n", unode->amt, unode->name());
			break;}
		case NODE_STRING:
			printf("string: \"%s\"\n", dynamic_cast<const StringNode*>(node)->s);
			break;
		case NODE_REGEX:
			printf("regex: \"%s\"\n", dynamic_cast<const StringNode*>(node)->s);
			break;
		case NODE_WILDCARD:
			printf("wildcard: \"*\"\n");
			break;
		case NODE_IDENTIFIER:
			printf("identifier: \"%s\"\n", dynamic_cast<const IdentifierNode*>(node)->sym->name.c_str());
			break;
		case NODE_LIST:{
				const ListNode *lnode = dynamic_cast<const ListNode*>(node);
				printf("list: %d\n", lnode->size());
				for (unsigned int i=0; i<lnode->size(); i++) {
					print_tree((*lnode)[i], depth+1);
				}
			}
			break;
		case NODE_OPERATOR:{
			const OperatorNode *onode = dynamic_cast<const OperatorNode*>(node);
			if (onode->op <= 255)
				printf("operator %d (%c)\n", onode->op, onode->op);
			else
				printf("operator %d (%s)\n", onode->op, get_op_name(onode->op));
			for (i=0; i<onode->nops(); i++)
				print_tree(onode->operands[i], depth+1);
			break;}
		default:
			fprintf(stderr, "unknown node type %d\n", node->type());
			assert(!"not reached");
	}
}

std::map<std::string, RecognizerBase*> recognizers_by_name;
std::vector<RecognizerBase*> recognizers;
std::vector<Aggregate*> aggregates;
bool this_path_ok = true;

void add_recognizer(RecognizerBase *r) {
	recognizers.push_back(r);
	recognizers_by_name[r->name] = r;
	//r->print();
}

void add_aggregate(Aggregate *a) {
	aggregates.push_back(a);
}
