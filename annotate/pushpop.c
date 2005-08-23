#include <unistd.h>
#include "annotate.h"

int main() {
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 1);
	ANNOTATE_START_TASK(NULL, 0, "foo");
	sleep(1);
	ANNOTATE_END_TASK(NULL, 0, "foo");
	ANNOTATE_PUSH_PATH_ID_STR(NULL, 0, "blah%d", 123);
	ANNOTATE_START_TASK(NULL, 0, "bar");
	sleep(1);
	ANNOTATE_END_TASK(NULL, 0, "bar");
	ANNOTATE_POP_PATH_ID(NULL, 0);
	ANNOTATE_START_TASK(NULL, 0, "bink");
	sleep(1);
	ANNOTATE_END_TASK(NULL, 0, "bink");
	return 0;
}
