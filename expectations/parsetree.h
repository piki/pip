#ifndef PARSE_TREE_H
#define PARSE_TREE_H

#define RANGE_INF -1

typedef enum { NODE_INT, NODE_STRING, NODE_REGEX, NODE_IDENTIFIER, NODE_OPERATOR } NodeType;
typedef enum {
	SYM_UNBOUND, SYM_PATH_DECL, SYM_PATH_VAR, SYM_STRING_VAR
} SymbolType;

typedef struct {
	SymbolType type;
	char *name;
} Symbol;

typedef struct {
	int value;
} IntNode;

typedef struct {
	char *s;
} StringNode;  /* also regex */

typedef struct {
	Symbol *sym;
} IdentifierNode;

typedef struct {
	int op;
	int noperands;
	struct _Node *operands[1];  /* expandable */
} OperatorNode;

typedef struct _Node {
	NodeType type;
	union {
		IntNode iconst;
		StringNode sconst;
		IdentifierNode id;
		OperatorNode opr;
	};
} Node;

void symbols_init(void);
/* Look for a symbol with the given name.  If none is found, create it. */
/* If an existing symbol is found with a different type, NULL is returned,
 * indicating error. */
Symbol *symbol_lookup(const char *name);

Node *constant_int(int n);
Node *opr(int which, int argc, ...);
Node *id(Symbol *sym);
Node *constant_string(const char *s);
Node *constant_regex(const char *s);

void print_tree(const Node *node, int depth);

#endif
