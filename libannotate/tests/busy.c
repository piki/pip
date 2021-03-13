/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "annotate.h"

int main() {
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 1);
	ANNOTATE_START_TASK(NULL, 0, "busy 50ms");
	struct timeval now, end;
	gettimeofday(&end, NULL);
	end.tv_usec += 50000;
	while (end.tv_usec >= 1000000) { end.tv_usec -= 1000000; end.tv_sec++; }
	do {
		gettimeofday(&now, NULL);
	} while (end.tv_sec > now.tv_sec || (end.tv_sec == now.tv_sec && end.tv_usec > now.tv_usec));
	ANNOTATE_END_TASK(NULL, 0, "busy 50ms");

	ANNOTATE_START_TASK(NULL, 0, "sleepy 50ms");
	usleep(50000);
	ANNOTATE_END_TASK(NULL, 0, "sleepy 50ms");
	return 0;
}
