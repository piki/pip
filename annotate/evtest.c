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
	ANNOTATE_SET_PATH_ID_INT(1);
	ANNOTATE_START_TASK("first parent");
	usleep(20000);
	printf("2\n");
	ANNOTATE_START_TASK("first child");
	usleep(40000);
	printf("4\n");
	ANNOTATE_SET_PATH_ID_INT(2);
	ANNOTATE_START_TASK("second");
	usleep(80000);
	printf("8\n");
	ANNOTATE_SET_PATH_ID_INT(1);
	usleep(160000);
	printf("16\n");
	ANNOTATE_END_TASK("first child");
	usleep(320000);
	printf("32\n");
	ANNOTATE_SET_PATH_ID_INT(2);
	usleep(640000);
	printf("64\n");
	ANNOTATE_END_TASK("second");
	ANNOTATE_SET_PATH_ID_INT(1);
	usleep(1280000);
	printf("128\n");
	ANNOTATE_END_TASK("first parent");
	return 0;
}
