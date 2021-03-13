#include <string.h>
#include "pipdb.h"

static void str_append_int(std::string *dest, int src) {
	dest->append(1, src & 0xff);
	dest->append(1, (src >> 8) & 0xff);
	dest->append(1, (src >> 16) & 0xff);
	dest->append(1, (src >> 24) & 0xff);
}

static int str_unpack_int(const char *str) {
	return (str[0] & 0xff) | ((str[1]<<8) & 0xff00) | ((str[2]<<16) & 0xff0000) | ((str[3]<<24) & 0xff000000);
}

std::string PipDBHeader::pack(void) const {
	std::string ret;
	ret.append(magic, 3);
	ret.append(1, version);
	str_append_int(&ret, first_ts.tv_sec);
	str_append_int(&ret, first_ts.tv_usec);
	str_append_int(&ret, last_ts.tv_sec);
	str_append_int(&ret, last_ts.tv_usec);
	str_append_int(&ret, threads_offset);
	str_append_int(&ret, nthreads);
	str_append_int(&ret, task_idx_offset);
	str_append_int(&ret, ntasks);
	str_append_int(&ret, path_idx_offset);
	str_append_int(&ret, npaths);
	return ret;
}

PipDBHeader PipDBHeader::unpack(const char *str) {
	PipDBHeader ret;
	strncpy(ret.magic, &str[0], 3);
	ret.version = str[3];
	ret.first_ts.tv_sec = str_unpack_int(&str[4]);
	ret.first_ts.tv_usec = str_unpack_int(&str[8]);
	ret.last_ts.tv_sec = str_unpack_int(&str[12]);
	ret.last_ts.tv_usec = str_unpack_int(&str[16]);
	ret.threads_offset = str_unpack_int(&str[20]);
	ret.nthreads = str_unpack_int(&str[24]);
	ret.task_idx_offset = str_unpack_int(&str[28]);
	ret.ntasks = str_unpack_int(&str[32]);
	ret.path_idx_offset = str_unpack_int(&str[36]);
	ret.npaths = str_unpack_int(&str[40]);

	PipDBHeader a = ret;
	return a;
}
