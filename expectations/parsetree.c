#include <stdarg.h>
#include <stdio.h>
#include <glib.h>
#include "parsetree.h"
#include "y.tab.h"

static GHashTable *symbol_table = NULL;

void symbols_init(void) {
	symbol_table = g_hash_table_new(g_str_hash, g_str_equal);
}

Symbol *symbol_lookup(const char *name) {
	Symbol *sym = g_hash_table_lookup(symbol_table, name);
	if (!sym) {
		char *copy = g_strdup(name);
		sym = g_new(Symbol, 1);
		sym->type = SYM_UNBOUND;
		sym->name = copy;
		g_hash_table_insert(symbol_table, copy, sym);
	}
	return sym;
}

Node *constant_int(int n) {
	Node *ret = g_new(Node, 1);
	ret->type = NODE_INT;
	ret->iconst.value = n;
	return ret;
}

Node *opr(int which, int argc, ...) {
	int i;
	va_list args;
	Node *ret = (Node*)g_malloc(sizeof(Node) + (argc-1)*sizeof(Node*));
	ret->type = NODE_OPERATOR;
	ret->opr.op = which;
	ret->opr.noperands = argc;
	va_start(args, argc);
	for (i=0; i<argc; i++)
		ret->opr.operands[i] = va_arg(args, Node*);
	va_end(args);
	return ret;
}

Node *id(Symbol *sym) {
	Node *ret = g_new(Node, 1);
	ret->type = NODE_IDENTIFIER;
	ret->id.sym = sym;
	return ret;
}

Node *constant_string(const char *s) {
	Node *ret = g_new(Node, 1);
	ret->type = NODE_STRING;
	ret->sconst.s = g_strdup(s);
	return ret;
}

Node *constant_regex(const char *s) {
	Node *ret = g_new(Node, 1);
	ret->type = NODE_REGEX;
	ret->sconst.s = g_strdup(s);
	return ret;
}

struct {
	int op;
	const char *name;
} opmap[] = {
	{ PATH, "PATH" },
	{ PATH_UNION, "PATH_UNION" },
	{ REPEAT, "REPEAT" },
	{ REVERSE, "REVERSE" },
	{ JOIN, "JOIN" },
	{ BRANCH, "BRANCH" },
	{ SPLIT, "SPLIT" },
	{ XOR, "XOR" },
	{ CALL, "CALL" },
	{ MESSAGE, "MESSAGE" },
	{ TASK, "TASK" },
	{ EVENT, "EVENT" },
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

void print_tree(const Node *node, int depth) {
	int i;
	printf("%*sNode %p: ", depth*2, "", node);
	if (!node) { printf("NULL\n"); return; }
	switch (node->type) {
		case NODE_INT:
			printf("int: %d\n", node->iconst.value);
			break;
		case NODE_STRING:
			printf("string: \"%s\"\n", node->sconst.s);
			break;
		case NODE_REGEX:
			printf("regex: \"%s\"\n", node->sconst.s);
			break;
		case NODE_IDENTIFIER:
			printf("identifier: \"%s\"\n", node->id.sym->name);
			break;
		case NODE_OPERATOR:
			if (node->opr.op <= 255)
				printf("operator %d (%c)\n", node->opr.op, node->opr.op);
			else {
				int found = 0;
				for (i=0; opmap[i].op > 0; i++)
					if (node->opr.op == opmap[i].op) {
						printf("operator %d (%s)\n", node->opr.op, opmap[i].name);
						found = 1;
						break;
					}
				if (!found)
					printf("operator %d (!UNKNOWN!)\n", node->opr.op);
			}
			for (i=0; i<node->opr.noperands; i++)
				print_tree(node->opr.operands[i], depth+1);
			break;
		default:
			g_assert_not_reached();
	}
}
