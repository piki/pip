#ifndef PARSE_TREE_H
#define PARSE_TREE_H

#include <map>
#include <string>
#include <vector>

#define RANGE_INF -1

typedef enum { NODE_INT, NODE_FLOAT, NODE_STRING, NODE_REGEX, NODE_WILDCARD, NODE_IDENTIFIER, NODE_OPERATOR, NODE_LIST, NODE_UNITS } NodeType;
typedef enum { SYM_METRIC, SYM_RECOGNIZER, SYM_PATH_VAR, SYM_STRING_VAR, SYM_BRANCH } SymbolType;

class Symbol {
public:
	Symbol(char *_name, SymbolType _type, bool _global);

	// Look for a symbol with the given name.  If none is found, create it.
	// If "excl" is set and the symbol already exists, it's an error
	static Symbol *create(char *name, SymbolType type, bool global,
			bool excl);
	// Look for a symbol with the given name.
	// If the symbol exists with a different type, it's an error
	static Symbol *find(const char *name, SymbolType type);

	// Clear out all non-global variables
	static void clear_locals(void);

	SymbolType type;
	std::string name;
	bool global;
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

class FloatNode : public Node {
public:
	FloatNode(float _value) : value(_value) {}
	inline NodeType type(void) const { return NODE_FLOAT; }
	float value;
};

class StringNode : public Node {
public:
	StringNode(NodeType type, const char *_s) :
		s(type == NODE_WILDCARD ? NULL : strdup(_s)), _type(type) {}
	~StringNode(void) { free(s); }
	inline NodeType type(void) const { return _type; }
	char *s;    /* string or regex */
private:
	NodeType _type;
};

#define idf(X,T) new IdentifierNode(Symbol::find(X,SYM_##T))
#define idcge(X,T) new IdentifierNode(Symbol::create(X,SYM_##T,true,true))
#define idcle(X,T) new IdentifierNode(Symbol::create(X,SYM_##T,false,true))
#define idcg(X,T) new IdentifierNode(Symbol::create(X,SYM_##T,true,false))
#define idcl(X,T) new IdentifierNode(Symbol::create(X,SYM_##T,false,false))
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
	inline unsigned int nops(void) const { return operands.size(); }
	int op;
	std::vector<Node*> operands;
};

enum UnitType {
	UNIT_NONE,
	UNIT_HOUR, UNIT_MIN, UNIT_SEC, UNIT_MSEC, UNIT_USEC, UNIT_NSEC,
	UNIT_BYTE, UNIT_KB, UNIT_MB, UNIT_GB, UNIT_TB,
	UNIT_LAST
};
class UnitsNode : public Node {
public:
	UnitsNode(float _amt, const char *_name);
	inline NodeType type(void) const { return NODE_UNITS; }
	float amt;
	UnitType unit;
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

const char *get_op_name(int op);
void print_tree(const Node *node, int depth);
void add_recognizer(const Node *node);
void add_aggregate(Node *node);

class Recognizer;
class Aggregate;
extern std::map<std::string, Recognizer*> recognizers;
extern std::vector<Aggregate*> aggregates;
extern bool expect_parse(const char *filename);

#endif
