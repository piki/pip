#ifndef AGGREGATES_H
#define AGGREGATES_H

#include <stdio.h>
#include "parsetree.h"

class Aggregate {
public:
	Aggregate(Node *_node);
	void print(FILE *fp = stdout) const;
	bool check(void) const;

private:
	float eval(const Node *n) const;

	Node *node;
};

#endif
