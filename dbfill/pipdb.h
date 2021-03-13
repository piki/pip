#ifndef PIPDB_H
#define PIPDB_H

#include <sys/time.h>
#include <string>

struct PipDBHeader {
	char magic[3];
	char version;
	timeval first_ts, last_ts;
	int threads_offset, nthreads;
	int task_idx_offset, ntasks;
	int path_idx_offset, npaths;

	std::string pack(void) const;
	static PipDBHeader unpack(const char *str);
};

#endif
