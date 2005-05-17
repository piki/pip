#include "pathstub.h"

PathStub::PathStub(const Path &path, int nrecognizers)
		: recognizers(nrecognizers) {
	utime = path.utime;
	stime = path.stime;
	major_fault = path.major_fault;
	minor_fault = path.minor_fault;
	vol_cs = path.vol_cs;
	invol_cs = path.invol_cs;
	ts_start = path.ts_start;
	ts_end = path.ts_end;
	size = path.size;
	messages = path.messages;
	depth = path.depth;
	hosts = path.hosts;
	path_id = path.path_id;
	valid = path.valid();
	validated = false;
}

void PathStub::print(FILE *fp) const {
	fprintf(fp, "PathStub: pathid=%d valid=%s utime=%d stime=%d majflt=%d "
		"minflt=%d vcs=%d ivcs=%d start=%ld.%06ld end=%ld.%06ld bytes=%d "
		"msgs=%d depth=%d hosts=%d\n", path_id, valid?"true":"false", utime, stime,
		major_fault, minor_fault, vol_cs, invol_cs, ts_start.tv_sec,
		ts_start.tv_usec, ts_end.tv_sec, ts_end.tv_usec, size, messages, depth, hosts);
}
