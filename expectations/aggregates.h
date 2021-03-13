/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef AGGREGATES_H
#define AGGREGATES_H

#include <math.h>
#include <stdio.h>
#include "parsetree.h"

class Aggregate {
public:
	Aggregate(Node *_node);
	void print(FILE *fp = stdout) const;
	void print_tree(FILE *fp = stdout) const;
	bool check(void) const;

private:
	float eval_float(const Node *n) const;
	bool eval_bool(const Node *n) const;

	Node *node;
};

class Counter {
public:
	Counter(void) : sum(0), sumsq(0), count(0) {}
	void add(float n);
	float min(void) const { return _min; }
	float max(void) const { return _max; }
	float avg(void) const { return sum/count; }
	float var(void) const { float mean = avg(); return sumsq/count - mean*mean; }
	float stddev(void) const { return sqrt(var()); }
private:
	float sum, sumsq, _min, _max;
	int count;
};

#endif
