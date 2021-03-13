/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef PARSE_TREE_H
#define PARSE_TREE_H

#include <assert.h>
#include <map>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>

#define RANGE_INF -1

typedef enum { NODE_INT, NODE_FLOAT, NODE_STRING, NODE_REGEX, NODE_WILDCARD, NODE_IDENTIFIER, NODE_OPERATOR, NODE_LIST, NODE_UNITS } NodeType;
typedef enum { SYM_METRIC, SYM_RECOGNIZER, SYM_STRING_VAR, SYM_INT_VAR, SYM_THREAD, SYM_FUTURE, SYM_ANY } SymbolType;

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
	union {
		int i;
		float f;
		//std::string s;
	} value;
};

class Node {
public:
	virtual NodeType type(void) const = 0;
	virtual ~Node(void) {};
	virtual int int_value(void) const { assert(!"no int value"); }
	virtual float float_value(void) const { assert(!"no float value"); }
	virtual std::string string_value(void) const { assert(!"no string value"); }
};

class IntNode : public Node {
public:
	IntNode(int _value) : value(_value) {}
	inline NodeType type(void) const { return NODE_INT; }
	virtual int int_value(void) const { return value; }
	int value;
};

class FloatNode : public Node {
public:
	FloatNode(float _value) : value(_value) {}
	inline NodeType type(void) const { return NODE_FLOAT; }
	virtual float float_value(void) const { return value; }
	float value;
};

class StringNode : public Node {
public:
	StringNode(NodeType type, const char *_s) :
		s(type == NODE_WILDCARD ? NULL : strdup(_s)), _type(type) {}
	~StringNode(void) { free(s); }
	inline NodeType type(void) const { return _type; }
	virtual std::string string_value(void) const { return s; }
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
	virtual int int_value(void) const { assert(sym->type == SYM_INT_VAR); return sym->value.i; }
	//virtual std::string string_value(void) const { assert(sym->type == SYM_STRING_VAR); return sym->value.s; }
	Symbol *sym;
};

#define opr(which, args...) new OperatorNode(which, args)
class OperatorNode : public Node {
public:
	OperatorNode(int which, int argc, ...);
	~OperatorNode(void);
	inline NodeType type(void) const { return NODE_OPERATOR; }
	inline unsigned int nops(void) const { return operands.size(); }
	virtual int int_value(void) const;
	//virtual float float_value(void) const;
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
	const char *name(void) const;
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

class RecognizerBase;
class Aggregate;

const char *get_op_name(int op);
void print_assert(FILE *fp, const Node *node);
void print_tree(const Node *node, int depth);
void add_recognizer(RecognizerBase *r);
void add_aggregate(Aggregate *a);

extern std::map<std::string, RecognizerBase*> recognizers_by_name;
extern std::vector<RecognizerBase*> recognizers;
extern std::vector<Aggregate*> aggregates;
extern bool expect_parse(const char *filename);
extern bool this_path_ok;

#endif
