#include <pthread.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/times.h>
#include "annotate.h"

void *start(void *arg) {
	int n = 2;
	while (1) {
		ANNOTATE_SET_PATH_ID(n++);
		ANNOTATE_START_TASK("child");
		printf("child\n"); fflush(stdout);
		int i;
		for (i=0; i<50000000; i++) ;
		ANNOTATE_END_TASK("child");
	}
}

int main() {
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID(1);
	pthread_t child;
	pthread_create(&child, NULL, start, NULL);
	while (1) {
#if 0
		struct tms clk;
		struct rusage ru;
		struct timespec tp;
		getrusage(RUSAGE_SELF, &ru);
		times(&clk);
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp) == -1)
			perror("clock_gettime");
		printf("parent: u=%ld.%06ld s=%ld.%06ld\n",
			ru.ru_utime.tv_sec, ru.ru_utime.tv_usec,
			ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
		printf("partms: u=%ld s=%ld cu=%ld cs=%ld\n",
			clk.tms_utime, clk.tms_stime, clk.tms_cutime, clk.tms_cstime);
		printf("parclk: proc=%ld.%09ld\n", tp.tv_sec, tp.tv_nsec);
		fflush(stdout);
#endif
		ANNOTATE_START_TASK("parent");
		printf("parent\n"); fflush(stdout);
		sleep(1);
		ANNOTATE_END_TASK("parent");
	}
	return 0;
}
