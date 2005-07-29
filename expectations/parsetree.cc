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

extern bool yy_success;
extern int yylno;
extern char *yyfilename;

Symbol *Symbol::create(char *name, SymbolType type, bool global,
		bool excl) {
	Symbol *sym = symbol_table[name];
	if (sym) {
		if (excl) {
			fprintf(stderr, "%s:%d: symbol \"%s\" redefined\n",
				yyfilename, yylno, name);
			yy_success = false;
		}
		else if (sym->type != type) {
			fprintf(stderr, "%s:%d: symbol \"%s\" type mismatch\n",
				yyfilename, yylno, name);
			yy_success = false;
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
		yy_success = false;
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
	yy_success = false;
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
		case UNIT_HOUR: amt *= 3600*1000000; unit = UNIT_USEC; break;
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
	{ RECOGNIZER, "RECOGNIZER" },
	{ FRAGMENT, "FRAGMENT" },
	{ REPEAT, "REPEAT" },
	{ BRANCH, "BRANCH" },
	{ XOR, "XOR" },
	{ CALL, "CALL" },
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
	{ LOG, "LOG" },
	{ LOGN, "LOGN" },
	{ SQRT, "SQRT" },
	{ EXP, "EXP" },
	{ F_POW, "F_POW" },
	{ GE, "GE" },
	{ LE, "LE" },
	{ EQ, "EQ" },
	{ NE, "NE" },
	{ B_AND, "B_AND" },
	{ B_OR, "B_OR" },
	{ IMPLIES, "IMPLIES" },
	{ IN, "IN" },
	{ RECV, "RECV" },
	{ SEND, "SEND" },
	{ TASK, "TASK" },
	{ THREAD, "THREAD" },
	{ NOTICE, "NOTICE" },
	{ LIMIT, "LIMIT" },
	{ RANGE, "RANGE" },
	{ STRING, "STRING" },
	{ REGEX, "REGEX" },
	{ INTEGER, "INTEGER" },
	{ IDENTIFIER, "IDENTIFIER" },
	{ STRINGVAR, "STRINGVAR" },
	{ PATHVAR, "PATHVAR" },
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
			fprintf(fp, "%d", ((IntNode*)node)->value);
			break;
		case NODE_FLOAT:
			fprintf(fp, "%f", ((FloatNode*)node)->value);
			break;
		case NODE_UNITS:
			fprintf(fp, "%.2f%s", ((UnitsNode*)node)->amt, ((UnitsNode*)node)->name());
			break;
		case NODE_STRING:
			fprintf(fp, "\"%s\"", ((StringNode*)node)->s);
			break;
		case NODE_REGEX:
			fprintf(fp, "m/%s/", ((StringNode*)node)->s);
			break;
		case NODE_IDENTIFIER:
			fprintf(fp, "%s", ((IdentifierNode*)node)->sym->name.c_str());
			break;
		case NODE_LIST:{
				ListNode *lnode = (ListNode*)node;										 	
				for (unsigned int i=0; i<lnode->size(); i++) {
					print_assert(fp, (*lnode)[i]);
				}
			}
			break;
		case NODE_OPERATOR:{
			OperatorNode *onode = (OperatorNode*)node;
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
			printf("int: %d\n", ((IntNode*)node)->value);
			break;
		case NODE_FLOAT:
			printf("float: %f\n", ((FloatNode*)node)->value);
			break;
		case NODE_UNITS:{
			UnitsNode *unode = (UnitsNode*)node;
			printf("units: %.2f %s\n", unode->amt, unode->name());
			break;}
		case NODE_STRING:
			printf("string: \"%s\"\n", ((StringNode*)node)->s);
			break;
		case NODE_REGEX:
			printf("regex: \"%s\"\n", ((StringNode*)node)->s);
			break;
		case NODE_WILDCARD:
			printf("wildcard: \"*\"\n");
			break;
		case NODE_IDENTIFIER:
			printf("identifier: \"%s\"\n", ((IdentifierNode*)node)->sym->name.c_str());
			break;
		case NODE_LIST:{
				ListNode *lnode = (ListNode*)node;										 	
				printf("list: %d\n", lnode->size());
				for (unsigned int i=0; i<lnode->size(); i++) {
					print_tree((*lnode)[i], depth+1);
				}
			}
			break;
		case NODE_OPERATOR:{
			OperatorNode *onode = (OperatorNode*)node;
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

std::map<std::string, Recognizer*> recognizers;
std::vector<Aggregate*> aggregates;

void add_recognizer(Recognizer *r) {
	recognizers[r->name] = r;
}

void add_aggregate(Aggregate *a) {
	aggregates.push_back(a);
}
