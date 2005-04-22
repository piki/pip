#include <stdio.h>
#include <unistd.h>
#include "annotate.h"

/* goal:
 *   "first parent" = 2 + 4 + 16 + 32 + 128 = 1,820,000
 *   "first child" = 4 + 16 = 20,000
 *   "second" = 8 + 64 = 72,000
 *   total = 254,000
 */

int main() {
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 1);
	ANNOTATE_START_TASK(NULL, 0, "first parent");
	usleep(20000);
	printf("2\n");
	ANNOTATE_START_TASK(NULL, 0, "first child");
	usleep(40000);
	printf("4\n");
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 2);
	ANNOTATE_START_TASK(NULL, 0, "second");
	usleep(80000);
	printf("8\n");
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 1);
	usleep(160000);
	printf("16\n");
	ANNOTATE_END_TASK(NULL, 0, "first child");
	usleep(320000);
	printf("32\n");
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 2);
	usleep(640000);
	printf("64\n");
	ANNOTATE_END_TASK(NULL, 0, "second");
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 1);
	usleep(1280000);
	printf("128\n");
	ANNOTATE_END_TASK(NULL, 0, "first parent");
	return 0;
}
