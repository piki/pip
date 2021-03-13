/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

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
	int size, messages, depth, hosts, latency;
	int bytes, threads;
	int path_id;
	bool valid, validated;
	BoolArray recognizers;
};

#endif
