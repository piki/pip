#ifndef EXPTREE_H
#define EXPTREE_H

#include <string>
#include <vector>
#include <stdio.h>
#include <pcre.h>
#include "aggregates.h"
#include "parsetree.h"
#include "path.h"

enum ExpEventType { EXP_TASK, EXP_NOTICE, EXP_MESSAGE_SEND, EXP_MESSAGE_RECV, EXP_REPEAT, EXP_XOR, EXP_SPLIT, EXP_CALL };

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
		MINOR_FAULTS, VOL_CS, INVOL_CS, LATENCY, SIZE, MESSAGES, DEPTH,
		THREADS, LAST };
	static Metric metric_by_name(const std::string &name);

	Limit(const OperatorNode *onode);
	void print(FILE *fp, int depth) const;
	bool check(const PathTask *test) const;
	bool check(const PathMessageSend *test) const;
	bool check(const Path *test) const;
	bool check(float n) const {
		return (min == -1 || n >= min) && (max == -1 || n <= max); }
private:
	float min, max;
	Metric metric;
};
typedef std::vector<Limit*> LimitList;

class ExpEvent {
public:
	virtual ~ExpEvent(void) {}
	virtual void print(FILE *fp, int depth) const = 0;
	virtual ExpEventType type(void) const = 0;

	// checks to see if this Event can match 0 or more path events, starting
	// at offset "ofs."  If yes, returns the number matched.  If no, returns
	// -1.
	// !! we need a way to return a continuation, i.e., for recursive
	// searches if we could have matched a varying number
	virtual int check(const std::vector<PathEvent*> &test, unsigned int ofs,
			bool *resources) const = 0;
};
typedef std::vector<ExpEvent*> ExpEventList;

class ExpThread {
public:
	ExpThread(const OperatorNode *onode);   // standard thread, in recognizer
	ExpThread(const ListNode *limit_list, const ListNode *statement);  // fragment thread
	~ExpThread(void);
	void print(FILE *fp) const;
	// not const, because it increments check itself
	bool check(const PathEventList &test, int ofs, bool fragment, bool *resources);

	int min, max, count;
	LimitList limits;
	ExpEventList events;
};
typedef std::map<std::string, ExpThread*> ExpThreadSet;

class ExpTask : public ExpEvent {
public:
	ExpTask(const OperatorNode *onode);
	virtual ~ExpTask(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_TASK; }
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	Match *name;
	LimitList limits;
	ExpEventList children;
};

class ExpNotice : public ExpEvent {
public:
	ExpNotice(const OperatorNode *onode);
	virtual ~ExpNotice(void) { delete name; }
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_NOTICE; }
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	Match *name;
};

class ExpMessageSend : public ExpEvent {
public:
	ExpMessageSend(const OperatorNode *onode);
	virtual ~ExpMessageSend(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_MESSAGE_SEND; }
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;
	LimitList limits;
};

class ExpMessageRecv : public ExpEvent {
public:
	ExpMessageRecv(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_MESSAGE_RECV; }
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;
	LimitList limits;
};

class ExpRepeat : public ExpEvent {
public:
	ExpRepeat(const OperatorNode *onode);
	virtual ~ExpRepeat(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_REPEAT; }
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
	virtual ExpEventType type(void) const { return EXP_XOR; }
	virtual int check(const PathEventList &test, unsigned int ofs,
			bool *resources) const;

	std::vector<ExpEventList> branches;
};

class ExpCall : public ExpEvent {
public:
	ExpCall(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_CALL; }
	virtual int check(const PathEventList &test, unsigned int ofs, bool *resources) const;

	std::string target;
};

class Recognizer {
public:
	Recognizer(const IdentifierNode *ident, const ListNode *limit_list, const ListNode *statements, bool _complete, bool _validating);
	~Recognizer(void);
	void print(FILE *fp = stdout) const;
	bool check(const Path *path, bool *resources);
	static int check(const PathEventList &test, const ExpEventList &list, int ofs,
			bool *resources);

	std::string name;
	ExpThreadSet threads;
	ExpThread *root;
	LimitList limits;
	bool complete;    // match full paths (true) or fragments (false)
	bool validating;  // classify matched paths as valid?

	int instances, unique;
	Counter real_time, utime, stime, cpu_time, major_fault, minor_fault;
	Counter	vol_cs, invol_cs, latency, size, messages, depth, threadcount;
};

#endif
