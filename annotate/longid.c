#include <stdio.h>
#include "annotate.h"

int main() {
	char X[200] = "";
	ANNOTATE_INIT();
	ANNOTATE_SET_PATH_ID(NULL, 0, X, 200);
	return 0;
}
