#ifndef EXPTREE_H
#define EXPTREE_H

#include <string>
#include <vector>
#include <stdio.h>
#include <pcre.h>
#include "aggregates.h"
#include "parsetree.h"
#include "path.h"

class Match {
public:
	Match(bool _negate) : negate(_negate) {}
	virtual ~Match(void) {}
	virtual const char *to_string(void) const = 0;
	virtual bool check(const std::string &test) const = 0;
	static Match *create(Node *node, bool negate);
protected:
	bool negate;
};

class StringMatch : public Match {
public:
	StringMatch(const std::string &_data, bool _negate) : Match(_negate), data(_data) {}
	virtual const char *to_string(void) const { return data.c_str(); }
	virtual bool check(const std::string &test) const;
private:
	std::string data;
};

class RegexMatch : public Match {
public:
	RegexMatch(const std::string &_data, bool _negate);
	virtual ~RegexMatch(void);
	virtual const char *to_string(void) const { return "m/.../"; }
	virtual bool check(const std::string &test) const;
private:
	pcre *regex;
	pcre_extra *study;
};

class AnyMatch : public Match {
public:
	AnyMatch(bool _negate) : Match(_negate) {}
	virtual const char *to_string(void) const { return "*"; }
	virtual bool check(const std::string &test) const { return !negate; }
};

class VarMatch : public Match {
public:
	VarMatch(const Symbol *_sym, bool _negate) : Match(_negate), sym(_sym) {}
	virtual const char *to_string(void) const { return sym->name.c_str(); }
	virtual bool check(const std::string &test) const;
private:
	const Symbol *sym;
};

class Limit { 
public:
	enum Metric { REAL_TIME=0, UTIME, STIME, CPU_TIME, MAJOR_FAULTS,
		MINOR_FAULTS, VOL_CS, INVOL_CS, LATENCY, SIZE, LAST };

	Limit(const OperatorNode *onode);
	void print(FILE *fp, int depth) const;
	bool check(const PathTask *test) const;
	bool check(const PathMessageSend *test) const;
	bool check(const Path *test) const;
	bool check(float n) const { return n >= min && n <= max; }
private:
	float min, max;
	Metric metric;
};
typedef std::vector<Limit*> LimitList;

class ExpNotice;
class ExpMessage;

class ExpEvent {
public:
	virtual ~ExpEvent(void) {}
	virtual void print(FILE *fp, int depth) const = 0;

	// checks to see if this Event can match 0 or more path events, starting
	// at offset "ofs."  If yes, returns the number matched.  If no, returns
	// -1.
	// !! we need a way to return a continuation, i.e., for recursive
	// searches if we could have matched a varying number
	virtual int check(const std::vector<PathEvent*> &test, unsigned int ofs,
			bool *resources) const = 0;
};
typedef std::vector<ExpEvent*> ExpEventList;

class ExpTask : public ExpEvent {
public:
	ExpTask(const OperatorNode *onode);
	virtual ~ExpTask(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	Match *name, *host;
	LimitList limits;
	ExpEventList children;
};

class ExpNotice : public ExpEvent {
public:
	ExpNotice(const OperatorNode *onode);
	virtual ~ExpNotice(void) { delete name; delete host; }
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	Match *name, *host;
};

class ExpMessage : public ExpEvent {
public:
	virtual ~ExpMessage(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;
	ExpTask *recip;
	LimitList limits;
};

class ExpRepeat : public ExpEvent {
public:
	ExpRepeat(const OperatorNode *onode);
	virtual ~ExpRepeat(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	int min, max;
	ExpEventList children;
};

class ExpXor : public ExpEvent {
public:
	ExpXor(const OperatorNode *onode);
	virtual ~ExpXor(void);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	std::vector<ExpEventList> branches;
};

class ExpCall : public ExpEvent {
public:
	ExpCall(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	std::string target;
};

class Recognizer {
public:
	Recognizer(const Node *node);
	~Recognizer(void);
	void print(FILE *fp = stdout) const;
	bool check(const Path *path, bool *resources) const;
	int check(const PathEventList &test, int ofs, bool *resources) const;
	static int check(const PathEventList &test, const ExpEventList &list, int ofs,
			bool *resources);

	Symbol *name;
	ExpEventList children;
	LimitList limits;
	bool complete;  /* match full paths (true) or fragments (false) */
};

#endif
