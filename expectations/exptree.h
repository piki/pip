/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef EXPTREE_H
#define EXPTREE_H

#include <string>
#include <vector>
#include <stdio.h>
#include <pcre.h>
#include "aggregates.h"
#include "parsetree.h"
#include "path.h"

enum ExpEventType {
	EXP_TASK, EXP_NOTICE, EXP_MESSAGE_SEND, EXP_MESSAGE_RECV, EXP_REPEAT,
	EXP_XOR, EXP_CALL, EXP_ANY, EXP_ASSIGN, EXP_EVAL, EXP_FUTURE, EXP_DONE
};

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
	virtual const char *to_string(void) const;
	virtual bool check(const std::string &test) const;
private:
	std::string data;
};

class RegexMatch : public Match {
public:
	RegexMatch(const std::string &_data, bool _negate);
	virtual ~RegexMatch(void);
	virtual const char *to_string(void) const;
	virtual bool check(const std::string &test) const;
private:
	const std::string data;
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
	enum Metric { REAL_TIME=0, UTIME, STIME, CPU_TIME, BUSY_TIME, MAJOR_FAULTS,
		MINOR_FAULTS, VOL_CS, INVOL_CS, LATENCY, SIZE, MESSAGES, DEPTH,
		THREADS, HOSTS, LAST };
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

class FutureCounts {
public:
	FutureCounts(int _sz) : sz(_sz), _total(0) {
		data = new unsigned short[sz];
		memset(data, 0, sz*sizeof(unsigned short));
	}
	FutureCounts(const FutureCounts &cp) : sz(cp.sz), _total(cp._total) {
		data = new unsigned short[sz];
		memcpy(data, cp.data, sz*sizeof(unsigned short));
	}
	~FutureCounts(void) { delete[] data; }
	inline void inc(int n) { data[n]++; _total++; }
	inline void dec(int n) { data[n]--; _total--; }
	inline unsigned short operator[](int n) const { return data[n]; }
	inline unsigned short total(void) const { return _total; }
	inline bool operator< (const FutureCounts &other) const {
		assert(sz == other.sz);
		return memcmp(data, other.data, sz*sizeof(unsigned short)) < 0;
	}
	unsigned short sz;
private:
	unsigned short _total;
	unsigned short *data;
};

class ExpMatch {
public:
	ExpMatch(int _count, bool _resources, const FutureCounts &fc) : futures(fc) {
		data = _count & 0x7FFF;
		if (_resources) data |= 0x8000;
	}
	inline unsigned int count() const { return data & 0x7FFF; }
	inline bool resources() const { return (data & 0x8000) != 0; }
	inline bool operator< (const ExpMatch &other) const {
		if (data < other.data) return true;
		if (data > other.data) return false;
		return futures < other.futures;
	}

	FutureCounts futures;
protected:
	unsigned short data;
};
typedef std::set<ExpMatch> MatchSet;

class FutureTable;
class ExpEvent {
public:
	virtual ~ExpEvent(void) {}
	virtual void print(FILE *fp, int depth) const = 0;
	virtual ExpEventType type(void) const = 0;

	// checks to see if this Event can match 0 or more path events, starting
	// at offset "ofs."  Returns a set of all possible match lengths (empty
	// set on failed match).
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const = 0;

	bool base_check(const PathEventList &test, unsigned int ofs, int needed, PathEventType type, const char *name) const;
};
typedef std::vector<ExpEvent*> ExpEventList;

class ExpTask : public ExpEvent {
public:
	ExpTask(const OperatorNode *onode);
	virtual ~ExpTask(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_TASK; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

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
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	Match *name;
};

class ExpMessageSend : public ExpEvent {
public:
	ExpMessageSend(const OperatorNode *onode);
	virtual ~ExpMessageSend(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_MESSAGE_SEND; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;
	LimitList limits;
};

class ExpMessageRecv : public ExpEvent {
public:
	ExpMessageRecv(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_MESSAGE_RECV; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;
	LimitList limits;
};

class ExpRepeat : public ExpEvent {
public:
	ExpRepeat(const OperatorNode *onode);
	virtual ~ExpRepeat(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_REPEAT; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	Node *min, *max;
	ExpEventList children;
};

class ExpXor : public ExpEvent {
public:
	ExpXor(const OperatorNode *onode);
	virtual ~ExpXor(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_XOR; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	std::vector<ExpEventList> branches;
};

class ExpCall : public ExpEvent {
public:
	ExpCall(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_CALL; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	std::string target;
};

class ExpAny : public ExpEvent {
public:
	ExpAny(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_ANY; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;
};

class ExpAssign : public ExpEvent {
public:
	ExpAssign(const OperatorNode *onode);
	virtual ~ExpAssign(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_ASSIGN; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	IdentifierNode *ident;
	ExpEventList child;
};

class ExpEval : public ExpEvent {
public:
	ExpEval(const Node *node);
	virtual ~ExpEval(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_EVAL; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	const Node *node;
};

class ExpFuture : public ExpEvent {
public:
	ExpFuture(const OperatorNode *onode);
	virtual ~ExpFuture(void);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_FUTURE; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	std::string name;
	ExpEventList children;
	int ft_pos;
};

class FutureTable {
public:
	void inline add(ExpFuture *future) { data.push_back(future); by_name[future->name] = future; }
	const inline ExpFuture *find(const std::string &name) const { return by_name.find(name)->second; }
	const inline ExpFuture *operator[] (unsigned int idx) const { return data[idx]; }
	const inline unsigned int count(void) const { return data.size(); }
private:
	std::vector<ExpFuture*> data;
	std::map<std::string, ExpFuture*> by_name;
};

class ExpDone : public ExpEvent {
public:
	ExpDone(const OperatorNode *onode);
	virtual void print(FILE *fp, int depth) const;
	virtual ExpEventType type(void) const { return EXP_DONE; }
	virtual MatchSet check(const PathEventList &test, unsigned int ofs, const FutureTable &ft, const FutureCounts &fc) const;

	std::string name;
};

class ExpThread {
public:
	ExpThread(const OperatorNode *onode);   // standard thread, in recognizer
	ExpThread(const ListNode *limit_list, const ListNode *statement);  // fragment thread
	~ExpThread(void);
	void print(FILE *fp) const;
	// not const, because it increments count itself
	bool check(const PathEventList &test, unsigned int ofs, bool fragment, bool *resources);

	int min, max, count;
	LimitList limits;
	ExpEventList events;

protected:
	FutureTable ft;
	void find_futures(ExpEventList &list);
	void find_futures(ExpEventList &list, int *seq);
};
typedef std::map<std::string, ExpThread*> ExpThreadSet;

class RecognizerBase {
public:
	RecognizerBase(const IdentifierNode *ident, int _pathtype);
	virtual ~RecognizerBase(void) {}
	virtual void print(FILE *fp = stdout) const = 0;
	// probably OK, and much more efficient, to key match_map on Symbol*, not std::string
	virtual bool check(const Path *path, bool *resources, std::map<std::string, bool> *match_map) = 0;
	virtual const char *type_string(void) const = 0;
	virtual void tally(const Path *path);

	std::string name;
	int pathtype;     // validator, invalidator, or recognizer?
	int instances, unique;

	Counter real_time, utime, stime, cpu_time, busy_time, major_fault;
	Counter minor_fault, vol_cs, invol_cs, latency, size, messages, depth;
	Counter hosts, threadcount;
};

class SetRecognizer : public RecognizerBase {
public:
	SetRecognizer(const IdentifierNode *ident, const Node *_bool_expr, int _pathtype);
	virtual void print(FILE *fp = stdout) const;
	virtual bool check(const Path *path, bool *resources, std::map<std::string, bool> *match_map);
	virtual const char *type_string(void) const { return "set"; }
	const Node *bool_expr;
};

class PathRecognizer : public RecognizerBase {
public:
	PathRecognizer(const IdentifierNode *ident, const ListNode *limit_list, const ListNode *statements, bool _complete, int _pathtype);
	~PathRecognizer(void);
	virtual void print(FILE *fp = stdout) const;
	virtual bool check(const Path *path, bool *resources, std::map<std::string, bool> *match_map);
	static MatchSet vlistmatch(const ExpEventList &list, unsigned int eofs,
			const PathEventList &test, unsigned int ofs,
			const FutureTable &ft, const FutureCounts &fc,
			bool match_all,           // must consume all path events
			bool match_all_futures,   // must satisfy all futures
			bool find_one,            // return after finding any one match
			bool allow_futures);      // bother to match futures at all?
	virtual const char *type_string(void) const { return complete ? "complete" : "fragment"; }

	ExpThreadSet threads;
	ExpThread *root;
	LimitList limits;
	bool complete;    // match full paths (true) or fragments (false)
};

const char *path_type_to_string(int pathtype);
extern bool debug_failed_matches;
extern std::string debug_buffer;

#endif
