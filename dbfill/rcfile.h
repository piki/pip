/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef RCFILE_H
#define RCFILE_H

#include <map>
#include <string>

class PipRC {
public:
	PipRC(void);
	inline const char *operator[] (const std::string &key) const {
		return get(key);
	}
	inline const char *get(const std::string &key, const char *dfl=NULL) const {
		std::map<std::string, std::string>::const_iterator p = data.find(key);
		if (p == data.end()) return dfl; else return p->second.c_str();
	}
private:
	std::map<std::string, std::string> data;
};

extern PipRC config;

#endif
