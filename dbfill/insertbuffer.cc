/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <mysql/mysql.h>
#include <map>
#include <string>
#include "insertbuffer.h"

#define MAX_LEN 4000000

static std::map<std::string, std::string> buffers;
namespace SqlBuffer {
static void init_buffer(const std::string &table, std::string &buf);
static void flush_one(const std::string &buf);
}

void SqlBuffer::insert(const std::string &table, const std::string &content) {
	std::string &buf = buffers[table];
	//printf("appending %d bytes to table %s\n", content.length(), table.c_str());
	if (buf.empty())
		init_buffer(table, buf);
	else if (buf.length() + content.length() + 3 > MAX_LEN) {
		flush_one(buf);
		init_buffer(table, buf);
	}
	else
		buf.append(",");
	buf.append(content);
}

void SqlBuffer::insert(const std::string &table, const char *fmt, ...) {
	char buf[65536];
	va_list arg;
	va_start(arg, fmt);
	vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);
	insert(table, std::string(buf));
}

static void SqlBuffer::init_buffer(const std::string &table, std::string &buf) {
	buf.assign("INSERT INTO ");
	buf.append(table);
	buf.append(" VALUES ");
}

extern void run_sql(const char *cmd);
static void SqlBuffer::flush_one(const std::string &buf) {
	//printf("flushing %d bytes to sql\n", buf.length());
	run_sql(buf.c_str());
}

void SqlBuffer::flush_all(void) {
	//printf("flushing everything\n");
	std::map<std::string, std::string>::const_iterator bp;
	for (bp=buffers.begin(); bp!=buffers.end(); bp++) {
		//printf("  %s => %d bytes\n", bp->first.c_str(), bp->second.size());
		flush_one(bp->second);
	}
	buffers.clear();
	assert(buffers.size() == 0);
}

static void check_empty(void) __attribute__((destructor));
static void check_empty(void) {
	if (buffers.size() == 0) return;
	fprintf(stderr, "Warning: SQL buffers not flushed!  Remember to call SqlBuffer::flush_all().\n");
	std::map<std::string, std::string>::const_iterator bp;
	for (bp=buffers.begin(); bp!=buffers.end(); bp++)
		fprintf(stderr, "  %s => %zd bytes\n", bp->first.c_str(), bp->second.size());
}
