#include <pthread.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/times.h>
#include "annotate.h"

void *start(void *arg) {
	int n = 3;
	ANNOTATE_SET_PATH_ID_INT(n++);
	ANNOTATE_START_TASK("sleep 50 ms");
	usleep(50000);
	ANNOTATE_END_TASK("sleep 50 ms");
	ANNOTATE_SET_PATH_ID_INT(2);
	ANNOTATE_END_TASK("catch-this");
	while (1) {
		ANNOTATE_SET_PATH_ID_INT(n++);
		ANNOTATE_START_TASK("child");
		printf("child\n"); fflush(stdout);
		int i;
		for (i=0; i<50000000; i++) ;
		ANNOTATE_END_TASK("child");
		ANNOTATE_BELIEF(rand()%2 == 0, 0.6);
	}
}

int main() {
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID_INT(1);
	int idsz, i;
	const unsigned char *path_id = ANNOTATE_GET_PATH_ID(&idsz);
	printf("parent's ID is ");
	for (i=0; i<idsz; i++)
		printf("%02x", path_id[i]);
	printf("\n");
	ANNOTATE_SET_PATH_ID_INT(2);
	ANNOTATE_START_TASK("catch-this");
	ANNOTATE_SET_PATH_ID_INT(1);
	pthread_t child;
	pthread_create(&child, NULL, start, NULL);
	ANNOTATE_BELIEF(1, 0);
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
