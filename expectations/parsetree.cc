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
			exit(1);
	}
}

ListNode::~ListNode(void) {
	for (unsigned int i=0; i<data.size(); i++)
		delete data[i];
}

struct {
	int op;
	const char *name;
} opmap[] = {
	{ EXPECTATION, "EXPECTATION" },
	{ FRAGMENT, "FRAGMENT" },
	{ REPEAT, "REPEAT" },
	{ REVERSE, "REVERSE" },
	{ JOIN, "JOIN" },
	{ BRANCH, "BRANCH" },
	{ SPLIT, "SPLIT" },
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
	{ GE, "GE" },
	{ LE, "LE" },
	{ EQ, "EQ" },
	{ NE, "NE" },
	{ B_AND, "B_AND" },
	{ B_OR, "B_OR" },
	{ IMPLIES, "IMPLIES" },
	{ IN, "IN" },
	{ MESSAGE, "MESSAGE" },
	{ TASK, "TASK" },
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

#if 0
/* ALMOST print out the input.
 * 1)   The "RANGE" operator is used in three different contexts:
 *       - repeat between 1 and 5 { ... }
 *       - during(1, 5) ...
 *       - limit(CPU_TIME, {1...5})
 *      We print it out as "{1...5}" and so it does not parse right in the
 *      "repeat" or "during" contexts.
 * 2)   Blank lines, spacing, and comments.  Duh.
 * 3)   Parentheses get lost.
 * 4)   "repeat" statements always get {} around their body
 * 5)   Indentation weirdness with @PATH=repeat
 */
#define TAB printf("%*s", 2*depth, "");
void print_tree(const Node *node, int depth) {
	if (!node) { return; }
	switch (node->type()) {
		case NODE_INT:
			printf("%d", ((IntNode*)node)->value);
			break;
		case NODE_STRING:
			printf("\"%s\"", ((StringNode*)node)->s);
			break;
		case NODE_REGEX:
			printf("m/%s/", ((StringNode*)node)->s);
			break;
		case NODE_IDENTIFIER:
			printf("%s", ((IdentifierNode*)node)->sym->name.c_str());
			break;
		case NODE_LIST:{
				ListNode *lnode = (ListNode*)node;										 	
				for (unsigned int i=0; i<lnode->size(); i++) {
					print_tree((*lnode)[i], depth);
				}
			}
			break;
		case NODE_OPERATOR:{
			OperatorNode *onode = (OperatorNode*)node;
			switch (onode->op) {
				case EXPECTATION:
					TAB printf("expectation ");
					print_tree(onode->operands[0], depth);
					printf(" {\n");
					print_tree(onode->operands[1], depth+1);
					TAB printf("}\n");
					break;
				case FRAGMENT:
					TAB printf("fragment ");
					print_tree(onode->operands[0], depth);
					printf(" {\n");
					print_tree(onode->operands[1], depth+1);
					TAB printf("}\n");
					break;
				case ASSERT:
					TAB printf("assert( ");
					print_tree(onode->operands[0], depth);
					printf(" )\n");
					break;
				case REVERSE:
					TAB printf("reverse(");
					print_tree(onode->operands[0], depth);
					printf(");\n");
					break;
				case MESSAGE:
					TAB printf("message();\n");
					break;
				case CALL:
					TAB printf("call(");
					print_tree(onode->operands[0], depth);
					printf(");\n");
					break;
				case TASK:
					switch (onode->nops()) {
						case 2:
							TAB printf("task(");
							print_tree(onode->operands[0], depth);
							printf(", ");
							print_tree(onode->operands[1], depth);
							printf(")");
							break;
						case 3:
							print_tree(onode->operands[0], depth);
							print_tree(onode->operands[1], depth+1);
							if (onode->operands[2]) {
								printf(" {\n");
								print_tree(onode->operands[2], depth+1);
								TAB printf("}\n");
							}
							else
								printf(";\n");
							break;
						default:
							assert(!"invalid number of operands for TASK");
					}
					break;
				case NOTICE:
					TAB printf("notice(");
					print_tree(onode->operands[0], depth);
					printf(", ");
					print_tree(onode->operands[1], depth);
					printf(");\n");
					break;
				case XOR:
					TAB printf("xor {\n");
					print_tree(onode->operands[0], depth+1);
					TAB printf("}\n");
					break;
				case LIMIT:
					printf("\n");
					TAB printf("limit(");
					print_tree(onode->operands[0], depth);
					printf(", ");
					print_tree(onode->operands[1], depth);
					printf(")");
					break;
				case RANGE:
					printf("{");
					print_tree(onode->operands[0], depth);
					printf("...");
					if (onode->operands[1]) print_tree(onode->operands[1], depth);
					printf("}");
					break;
				case BRANCH:
					TAB printf("branch ");
					print_tree(onode->operands[0], depth);
					printf(":\n");
					print_tree(onode->operands[1], depth+1);
					break;
				case SPLIT:
					TAB printf("split {\n");
					print_tree(onode->operands[0], depth+1);
					TAB printf("} join (");
					print_tree(onode->operands[1], depth+1);
					printf(");\n");
					break;
				case REPEAT:
					TAB printf("repeat ");
					print_tree(onode->operands[0], depth);
					printf(" {\n");
					print_tree(onode->operands[1], depth+1);
					TAB printf("}\n");
					break;
				case '=':
					print_tree(onode->operands[0], depth);
					printf("%c", onode->op);
					print_tree(onode->operands[1], depth);
					break;
				case '<':
				case '>':
					print_tree(onode->operands[0], depth);
					printf(" %c ", onode->op);
					print_tree(onode->operands[1], depth);
					break;
				case DURING:
					TAB printf("during(");
					print_tree(onode->operands[0], depth);
					printf(") ");
					print_tree(onode->operands[1], depth);
					break;
				case ANY:
					printf("any ");
					print_tree(onode->operands[0], depth);
					break;
				case B_AND:
					print_tree(onode->operands[0], depth);
					printf(" && ");
					print_tree(onode->operands[1], depth);
					break;
				case B_OR:
					print_tree(onode->operands[0], depth);
					printf(" || ");
					print_tree(onode->operands[1], depth);
					break;
				case IMPLIES:
					print_tree(onode->operands[0], depth);
					printf(" -> ");
					print_tree(onode->operands[1], depth);
					break;
				case IN:
					print_tree(onode->operands[0], depth);
					printf(" in ");
					print_tree(onode->operands[1], depth);
					break;
				case LE:
					print_tree(onode->operands[0], depth);
					printf(" <= ");
					print_tree(onode->operands[1], depth);
					break;
				case GE:
					print_tree(onode->operands[0], depth);
					printf(" >= ");
					print_tree(onode->operands[1], depth);
					break;
				case EQ:
					print_tree(onode->operands[0], depth);
					printf(" == ");
					print_tree(onode->operands[1], depth);
					break;
				case NE:
					print_tree(onode->operands[0], depth);
					printf(" != ");
					print_tree(onode->operands[1], depth);
					break;
				case '!':
					printf("!");
					print_tree(onode->operands[0], depth);
					break;
				case INSTANCES:
					printf("instances(");
					print_tree(onode->operands[0], depth);
					printf(")");
					break;
				case UNIQUE:
					printf("unique(");
					print_tree(onode->operands[0], depth);
					printf(")");
					break;
				case F_MAX:
					printf("max(");
					print_tree(onode->operands[0], depth);
					printf(", ");
					print_tree(onode->operands[1], depth);
					printf(")");
					break;
				case AVERAGE:
					printf("average(");
					print_tree(onode->operands[0], depth);
					printf(", ");
					print_tree(onode->operands[1], depth);
					printf(")");
					break;
				case STDDEV:
					printf("stddev(");
					print_tree(onode->operands[0], depth);
					printf(", ");
					print_tree(onode->operands[1], depth);
					printf(")");
					break;
				default:
					printf("\n\n\nunhandled operator: ");
					if (onode->op <= 255)
						printf("%d (%c)\n", onode->op, onode->op);
					else
						printf("%d (%s)\n", onode->op, get_op_name(onode->op));
					exit(1);
			}
			break;}
		default:
			assert(!"not reached");
	}
}
#else
void print_tree(const Node *node, int depth) {
	unsigned int i;
	printf("%*sNode %p: ", depth*2, "", node);
	if (!node) { printf("NULL\n"); return; }
	switch (node->type()) {
		case NODE_INT:
			printf("int: %d\n", ((IntNode*)node)->value);
			break;
		case NODE_STRING:
			printf("string: \"%s\"\n", ((StringNode*)node)->s);
			break;
		case NODE_REGEX:
			printf("regex: \"%s\"\n", ((StringNode*)node)->s);
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
		case NODE_UNITS:{
			UnitsNode *unode = (UnitsNode*)node;
			printf("%f %s\n", unode->amt, get_unit_name(unode->unit));
			break;}
		default:
			fprintf(stderr, "unknown node type %d\n", node->type());
			assert(!"not reached");
	}
}
#endif

#undef TAB
#define TAB
void add_assert(const Node *node) {
	return;
	if (!node) { return; }
	switch (node->type()) {
		case NODE_INT:
			printf("%d", ((IntNode*)node)->value);
			break;
		case NODE_STRING:
			printf("\"%s\"", ((StringNode*)node)->s);
			break;
		case NODE_REGEX:
			printf("m/%s/", ((StringNode*)node)->s);
			break;
		case NODE_IDENTIFIER:
			printf("%s", ((IdentifierNode*)node)->sym->name.c_str());
			break;
		case NODE_OPERATOR:{
			OperatorNode *onode = (OperatorNode*)node;
			switch (onode->op) {
				case ASSERT:
					TAB printf("assert( ");
					add_assert(onode->operands[0]);
					printf(" )\n");
					break;
				case RANGE:
					printf("{");
					add_assert(onode->operands[0]);
					printf("...");
					if (onode->operands[1]) add_assert(onode->operands[1]);
					printf("}");
					break;
				case '<':
				case '>':
					add_assert(onode->operands[0]);
					printf(" %c ", onode->op);
					add_assert(onode->operands[1]);
					break;
				case DURING:
					TAB printf("during(");
					add_assert(onode->operands[0]);
					printf(") ");
					add_assert(onode->operands[1]);
					break;
				case ANY:
					printf("any ");
					add_assert(onode->operands[0]);
					break;
				case B_AND:
					add_assert(onode->operands[0]);
					printf(" && ");
					add_assert(onode->operands[1]);
					break;
				case B_OR:
					add_assert(onode->operands[0]);
					printf(" || ");
					add_assert(onode->operands[1]);
					break;
				case IMPLIES:
					add_assert(onode->operands[0]);
					printf(" -> ");
					add_assert(onode->operands[1]);
					break;
				case IN:
					add_assert(onode->operands[0]);
					printf(" in ");
					add_assert(onode->operands[1]);
					break;
				case LE:
					add_assert(onode->operands[0]);
					printf(" <= ");
					add_assert(onode->operands[1]);
					break;
				case GE:
					add_assert(onode->operands[0]);
					printf(" >= ");
					add_assert(onode->operands[1]);
					break;
				case EQ:
					add_assert(onode->operands[0]);
					printf(" == ");
					add_assert(onode->operands[1]);
					break;
				case NE:
					add_assert(onode->operands[0]);
					printf(" != ");
					add_assert(onode->operands[1]);
					break;
				case '!':
					printf("!");
					add_assert(onode->operands[0]);
					break;
				case INSTANCES:
					printf("instances(");
					add_assert(onode->operands[0]);
					printf(")");
					break;
				case UNIQUE:
					printf("unique(");
					add_assert(onode->operands[0]);
					printf(")");
					break;
				case F_MAX:
					printf("max(");
					add_assert(onode->operands[0]);
					printf(", ");
					add_assert(onode->operands[1]);
					printf(")");
					break;
				case AVERAGE:
					printf("average(");
					add_assert(onode->operands[0]);
					printf(", ");
					add_assert(onode->operands[1]);
					printf(")");
					break;
				case STDDEV:
					printf("stddev(");
					add_assert(onode->operands[0]);
					printf(", ");
					add_assert(onode->operands[1]);
					printf(")");
					break;
				default:
					printf("\n\n\nunhandled operator: ");
					if (onode->op <= 255)
						printf("%d (%c)\n", onode->op, onode->op);
					else
						printf("%d (%s)\n", onode->op, get_op_name(onode->op));
					exit(1);
			}
			break;}
		default:
			fprintf(stderr, "invalid node type %d\n", node->type());
			assert(!"not reached");
	}
}

std::vector<Recognizer*> recognizers;

void add_recognizer(const Node *node) {
	Recognizer *r = new Recognizer(node);
	recognizers.push_back(r);
}
