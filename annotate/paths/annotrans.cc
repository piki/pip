#include <stdio.h>
#include "events.h"

#if 0
void write_int(char *p, int n) {
	p[0] = (n >> 24) & 0xFF;
	p[1] = (n >> 16) & 0xFF;
	p[2] = (n >> 8) & 0xFF;
	p[3] = n & 0xFF;
}

void test(int into) {
	char buf[4];
	int out;
	write_int(buf, into);
	scan(buf, INT, &out, END);
	if (into != out) printf("%d -> %d (%d %d %d %d)\n",
		into, out, buf[0], buf[1], buf[2], buf[3]);
}
#endif

int main(int argc, char **argv) {
#if 0
	int i;
	for (i=-20000000; i<20000000; i++)
		test(i * 100);

	return 0;
#endif

	FILE *fp = fopen(argv[1], "r");
	if (!fp) { perror(argv[1]); return 1; }
	Event *e = read_event(fp);
	while (e) {
		e->print();
		delete e;
		e = read_event(fp);
	}
	return 0;
}
