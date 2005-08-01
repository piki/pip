#ifndef PATHSTUB_H
#define PATHSTUB_H

#include <stdio.h>
#include "boolarray.h"
#include "path.h"

class PathStub {
public:
	PathStub(const Path &path, int nrecognizers);
	void print(FILE *fp = stdout) const;

	int utime, stime, major_fault, minor_fault, vol_cs, invol_cs;
	timeval ts_start, ts_end;
	int size, messages, depth, hosts;
	int bytes, threads;
	int path_id;
	bool valid, validated;
	BoolArray recognizers;
};

#endif
