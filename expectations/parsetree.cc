#include <map>
#include <string>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "parsetree.h"
#include "expect.tab.hh"
#include "exptree.h"

static std::map<std::string, Symbol*> symbol_table;

Symbol *symbol_lookup(const char *name) {
	Symbol *sym = symbol_table[name];
	if (!sym) {
		sym = new Symbol(name);
		symbol_table[name] = sym;
	}
	return sym;
}

OperatorNode::OperatorNode(int which, int argc, ...) {
	op = which;
	noperands = argc;
	va_list args;
	va_start(args, argc);
	for (int i=0; i<argc; i++)
		operands.push_back(va_arg(args, Node*));
	va_end(args);
}

OperatorNode::~OperatorNode(void) {
	for (unsigned int i=0; i<operands.size(); i++)
		delete operands[i];
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
	{ ELLIPSIS, "ELLIPSIS" },
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
					switch (onode->noperands) {
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
					switch (onode->noperands) {
						case 1:
							TAB printf("branch:\n");
							print_tree(onode->operands[0], depth+1);
							break;
						case 2:
							TAB printf("branch ");
							print_tree(onode->operands[0], depth);
							printf(":\n");
							print_tree(onode->operands[1], depth+1);
							break;
						default:
							assert(!"invalid number of operands for BRANCH");
					}
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
	int i;
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
			for (i=0; i<onode->noperands; i++)
				print_tree(onode->operands[i], depth+1);
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
