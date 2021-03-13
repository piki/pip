/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#ifndef INSERTBUFFER_H
#define INSERTBUFFER_H

#include <map>
#include <string>

namespace SqlBuffer {
void insert(const std::string &table, const std::string &content);
void insert(const std::string &table, const char *fmt, ...) __attribute__((__format__(printf,2,3)));
void flush_all(void);
}

#endif
