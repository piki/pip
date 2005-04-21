#include <stdio.h>
#include <string.h>
#include <map>
#include "events.h"

struct BeliefStat {
	BeliefFirst *bf;
	int yes, no;
};

int main(int argc, char **argv) {
	int i;
	if (argc < 2) {
		fprintf(stderr, "Usage:\n  %s file [file [file [...]]]\n", argv[0]);
		return 1;
	}

	std::map<int, BeliefStat> beliefs;

	for (i=1; i<argc; i++) {
		int version = -1;
		FILE *fp = fopen(argv[i], "r");
		if (!fp) { perror(argv[i]); continue; }
		Event *e = read_event(version, fp);
		while (e) {
			switch (e->type()) {
				case EV_HEADER:
					version = ((Header*)e)->version;
					delete e;
				case EV_BELIEF_FIRST:{
					BeliefFirst *bf = (BeliefFirst*)e;
					beliefs[bf->seq].bf = bf;
					beliefs[bf->seq].yes = beliefs[bf->seq].no = 0;
					break;
				}
				case EV_BELIEF:{
					Belief *b = (Belief*)e;
					if (b->cond)
						beliefs[b->seq].yes++;
					else
						beliefs[b->seq].no++;
					delete e;
					break;
				}
				default:
					delete e;
			}
			e = read_event(version, fp);
		}
		fclose(fp);
		printf("%d beliefs\n", beliefs.size());
		for (std::map<int, BeliefStat>::const_iterator bp=beliefs.begin(); bp!=beliefs.end(); bp++) {
			bp->second.bf->print(stdout, 1);
			float fail_rate = (float)bp->second.no / (bp->second.yes + bp->second.no);
			printf("    fail rate = %d/%d = %f\n", bp->second.no, bp->second.yes+bp->second.no, fail_rate);
			if (fail_rate > bp->second.bf->max_fail_rate) puts("    oops!"); else puts("    OK!");
		}
		beliefs.clear();
	}
	return 0;
}
