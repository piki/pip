#ifndef EXPTREE_H
#define EXPTREE_H

#include <string>
#include <vector>
#include <stdio.h>
#include <pcre.h>
#include "parsetree.h"
#include "path.h"

class Match {
public:
	virtual ~Match(void) {}
	virtual const char *to_string(void) const = 0;
	virtual bool check(const std::string &test) const = 0;
	static Match *create(StringNode *node);
};

class StringMatch : public Match {
public:
	StringMatch(const std::string &_data) : data(_data) {}
	virtual const char *to_string(void) const { return data.c_str(); }
	virtual bool check(const std::string &test) const;
private:
	std::string data;
};

class RegexMatch : public Match {
public:
	RegexMatch(const std::string &_data);
	virtual ~RegexMatch(void);
	virtual const char *to_string(void) const { return "m/.../"; }
	virtual bool check(const std::string &test) const;
private:
	pcre *regex;
	pcre_extra *study;
};

class VarMatch : public Match {
public:
	VarMatch(const Symbol *_sym) : sym(_sym) {}
	virtual const char *to_string(void) const { return sym->name.c_str(); }
	virtual bool check(const std::string &test) const;
private:
	const Symbol *sym;
};

class ExpNotice;
class ExpMessage;
class ExpLimit;

class ExpEvent {
public:
	virtual ~ExpEvent(void) {}
	virtual void print(FILE *fp, int depth) const = 0;

	// checks to see if this Event can match 0 or more path events, starting
	// at offset "ofs."  If yes, returns the number matched.  If no, returns
	// -1.
	// !! we need a way to return a continuation, i.e., for recursive
	// searches if we could have matched a varying number
	virtual int check(const std::vector<PathEvent*> &test, unsigned int ofs) const = 0;
};
typedef std::vector<ExpEvent*> ExpEventList;

class ExpTask : public ExpEvent {
public:
	ExpTask(const OperatorNode *onode);
	virtual ~ExpTask(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs) const;

	Match *name, *host;
	ExpEventList children;
};

class ExpNotice : public ExpEvent {
public:
	ExpNotice(const OperatorNode *onode);
	virtual ~ExpNotice(void) { delete name; delete host; }
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs) const;

	Match *name, *host;
};

class ExpMessage : public ExpEvent {
public:
	virtual ~ExpMessage(void) {}
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs) const;
	ExpTask *recip;
};

class ExpRepeat : public ExpEvent {
public:
	ExpRepeat(const OperatorNode *onode);
	virtual ~ExpRepeat(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs) const;

	int min, max;
	ExpEventList children;
};

class ExpXor : public ExpEvent {
public:
	ExpXor(const OperatorNode *onode);
	virtual ~ExpXor(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs) const;

	std::vector<ExpEventList> branches;
};

class Recognizer {
public:
	Recognizer(const Node *node);
	~Recognizer(void);
	void print(FILE *fp = stdout) const;
	void add_statements(const ListNode *node, ExpEventList *where);
	bool check(const Path &path) const;
	static int check(const PathEventList &test, const ExpEventList &list, int ofs);

	Symbol *name;
	ExpEventList children;
	bool complete;  /* match full paths (true) or fragments (false) */
};

#endif
