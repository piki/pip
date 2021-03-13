/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rcfile.h"

PipRC config;

PipRC::PipRC(void) {
	std::string filename(getenv("HOME"));
	filename.append("/.piprc");
	FILE *fp = fopen(filename.c_str(), "r");
	if (!fp) { perror(filename.c_str()); return; }

	char buf[512];
	int lineno = 0;
	while (fgets(buf, sizeof(buf), fp)) {
		++lineno;

		char *hash;
		if ((hash = strchr(buf, '#')) != NULL) *hash = '\0';

		char *eq, *key_start, *key_end, *val_start, *val_end;
		for (key_start=buf; isspace(*key_start); ++key_start)  ;
		if (*key_start == '\0') continue;   // empty line
		if ((eq = strchr(key_start, '=')) == NULL) {
			fprintf(stderr, "%s:%d: no '=' found\n", filename.c_str(), lineno);
			continue;
		}
		for (key_end=eq-1; key_end>key_start && isspace(*key_end); --key_end)  ;
		if (key_end < key_start) {
			fprintf(stderr, "%s:%d: blank key\n", filename.c_str(), lineno);
			continue;
		}

		for (val_start=eq+1; isspace(*val_start); ++val_start)  ;
		for (val_end=val_start+strlen(val_start)-1; val_end>=val_start && isspace(*val_end); --val_end)  ;

		std::string key(key_start, key_end-key_start+1);
		std::string value(val_start, val_end-val_start+1);
		data[key] = value;
	}
}
