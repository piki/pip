/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <stdlib.h>
#include "annotate.h"

int main() {
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID_INT(NULL, 0, 1);
	ANNOTATE_BELIEF(NULL, 0, 88*99==8712, 0.1);
	int i;
	for (i=0; i<100; i++) {
		ANNOTATE_BELIEF(NULL, 0, rand()%2==0, 0.3);
		ANNOTATE_BELIEF(NULL, 0, rand()%3==0, 0.8);
	}
	ANNOTATE_END_PATH_ID_INT(NULL, 0, 1);
	return 0;
}
