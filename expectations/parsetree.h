#ifndef PARSE_TREE_H
#define PARSE_TREE_H

#include <string>
#include <vector>

#define RANGE_INF -1

typedef enum { NODE_INT, NODE_STRING, NODE_REGEX, NODE_IDENTIFIER, NODE_OPERATOR, NODE_LIST } NodeType;
typedef enum {
	SYM_UNBOUND, SYM_PATH_DECL, SYM_PATH_VAR, SYM_STRING_VAR
} SymbolType;

class Symbol {
public:
	Symbol(const std::string &_name) : type(SYM_UNBOUND), name(_name) {}
	SymbolType type;
	std::string name;
};

class Node {
public:
	virtual NodeType type(void) const = 0;
	virtual ~Node(void) {};
};

class IntNode : public Node {
public:
	IntNode(int _value) : value(_value) {}
	inline NodeType type(void) const { return NODE_INT; }
	int value;
};

class StringNode : public Node {
public:
	StringNode(bool regex, const char *_s) :
		s(strdup(_s)), _type(regex?NODE_REGEX:NODE_STRING) {}
	~StringNode(void) { free(s); }
	inline NodeType type(void) const { return _type; }
	char *s;    /* string or regex */
private:
	NodeType _type;
};

#define id(X) new IdentifierNode(X)
class IdentifierNode : public Node {
public:
	IdentifierNode(Symbol *_sym) : sym(_sym) {}
	inline NodeType type(void) const { return NODE_IDENTIFIER; }
	Symbol *sym;
};

#define opr(which, args...) new OperatorNode(which, args)
class OperatorNode : public Node {
public:
	OperatorNode(int which, int argc, ...);
	~OperatorNode(void);
	inline NodeType type(void) const { return NODE_OPERATOR; }
	int op, noperands;
	std::vector<Node*> operands;
};

class ListNode : public Node {
public:
	~ListNode(void);
	inline NodeType type(void) const { return NODE_LIST; }
	inline void add(Node *node) { data.push_back(node); }
	inline unsigned int size(void) const { return data.size(); }
	inline const Node *operator[] (int n) const { return data[n]; }
private:
	std::vector<Node*> data;
};

void symbols_init(void);
/* Look for a symbol with the given name.  If none is found, create it. */
/* If an existing symbol is found with a different type, NULL is returned,
 * indicating error. */
Symbol *symbol_lookup(const char *name);


void print_tree(const Node *node, int depth);

#endif
