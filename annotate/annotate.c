#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "annotate.h"

#define BASEPATH "/tmp"
#define MAGIC 0x416e6e6f  // 'Anno'
#define VERSION 1

typedef struct {
	FILE *fp;
	int path_id;
} ThreadContext;

#ifdef THREADS
#include <pthread.h>
static pthread_key_t ctx_key;
#define GET_CTX ({ ThreadContext *_pctx = pthread_getspecific(ctx_key); \
		if (!_pctx) _pctx = new_context(); \
		_pctx; })
ThreadContext *new_context() {
	ThreadContext *pctx = malloc(sizeof(ThreadContext));
	char fn[256];
	sprintf(fn, BASEPATH"/trace-%d-%x", getpid(), (int)pthread_self());
	pctx->fp = fopen(fn, "w");
	if (!pctx->fp) { perror(fn); exit(1); }
	pctx->path_id = -1;
	pthread_setspecific(ctx_key, pctx);
	return pctx;
}
#else  /* no threads */
static ThreadContext ctx;
#define GET_CTX &ctx
#endif

typedef enum { STRING, CHAR, INT, END } OutType;
static void output(FILE *fp, ...);

void ANNOTATE_INIT(void) {
#ifdef THREADS
	ThreadContext *pctx = malloc(sizeof(ThreadContext));
	pthread_key_create(&ctx_key, free);
	pthread_setspecific(ctx_key, pctx);
#else
	ThreadContext *pctx = &ctx;
#endif

	/* open the output file */
	char fn[256];
	sprintf(fn, BASEPATH"/trace-%d", getpid());
	pctx->fp = fopen(fn, "w");
	if (!pctx->fp) { perror(fn); exit(1); }
	pctx->path_id = -1;

	/* prepare values for the header */
	char hostname[1024];
	char processbuf[1024], *processname = NULL;
	FILE *inp;
	struct timeval tv;
	struct timezone tz;
	pid_t pid, ppid;
	uid_t uid;

	gethostname(hostname, sizeof(hostname));
	gettimeofday(&tv, &tz);
	pid = getpid();
	ppid = getppid();
	uid = getuid();
#ifdef linux
	inp = fopen("/proc/self/status", "r");  /* Linuxism! */
	if (inp) {
		while (fgets(processbuf, sizeof(processbuf), inp)) {
			if (!strncasecmp(processbuf, "Name:", 5)) {
				char *p;
				p = strchr(processbuf, '\n');
				*p = '\0';
				p = strchr(processbuf, ':');
				++p;
				while (*p == ' ' || *p == '\t')  p++;
				processname = p;
				break;
			}
		}
		fclose(inp);
	}
#else
#warning The process name will not be present on non-Linux platforms
#endif
	
	output(pctx->fp,
		CHAR, 'H',
		INT, MAGIC,
		INT, VERSION,
		STRING, hostname,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, tz.tz_minuteswest,
		INT, pid,
		INT, ppid,
		INT, uid,
		STRING, processname ? processname : "",
		END);
}

void ANNOTATE_START_TASK(const char *name) {
	struct rusage ru;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	getrusage(RUSAGE_SELF, &ru);
	output(pctx->fp,
		CHAR, 'T',
		INT, pctx->path_id,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, ru.ru_utime.tv_sec, INT, ru.ru_utime.tv_usec,
		INT, ru.ru_stime.tv_sec, INT, ru.ru_stime.tv_usec,
		INT, ru.ru_minflt,  // minor page faults -- usually process growing
		INT, ru.ru_majflt,  // major page faults -- spin the disk
		INT, ru.ru_nvcsw,   // voluntary context switches -- block on something
		INT, ru.ru_nivcsw,  // involuntary context switches -- cpu hog
		STRING, name,
		END);
}

void ANNOTATE_END_TASK(const char *name) {
	struct rusage ru;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	getrusage(RUSAGE_SELF, &ru);
	output(pctx->fp,
		CHAR, 't',
		INT, pctx->path_id,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, ru.ru_utime.tv_sec, INT, ru.ru_utime.tv_usec,
		INT, ru.ru_stime.tv_sec, INT, ru.ru_stime.tv_usec,
		INT, ru.ru_minflt,  // minor page faults -- usually process growing
		INT, ru.ru_majflt,  // major page faults -- spin the disk
		INT, ru.ru_nvcsw,   // voluntary context switches -- block on something
		INT, ru.ru_nivcsw,  // involuntary context switches -- cpu hog
		STRING, name,
		END);
}

void ANNOTATE_SET_PATH_ID(unsigned int path_id) {
	ThreadContext *pctx = GET_CTX;
	if (pctx->path_id == path_id) return;
	pctx->path_id = path_id;

	struct rusage ru;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	getrusage(RUSAGE_SELF, &ru);
	output(pctx->fp,
		CHAR, 'P',
		INT, pctx->path_id,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, ru.ru_utime.tv_sec, INT, ru.ru_utime.tv_usec,
		INT, ru.ru_stime.tv_sec, INT, ru.ru_stime.tv_usec,
		INT, ru.ru_minflt,  // minor page faults -- usually process growing
		INT, ru.ru_majflt,  // major page faults -- spin the disk
		INT, ru.ru_nvcsw,   // voluntary context switches -- block on something
		INT, ru.ru_nivcsw,  // involuntary context switches -- cpu hog
		END);
}

void ANNOTATE_END_PATH_ID(unsigned int path_id) {
	ThreadContext *pctx = GET_CTX;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'p',
		INT, pctx->path_id,
		INT, tv.tv_sec, INT, tv.tv_usec,
		END);
}

void ANNOTATE_NOTICE(const char *fmt, ...) {
	va_list args;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	char buf[256 - 1 - 13];
	gettimeofday(&tv, NULL);
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	output(pctx->fp,
		CHAR, 'N',
		INT, pctx->path_id,
		INT, tv.tv_sec, INT, tv.tv_usec,
		STRING, buf,
		END);
}

void ANNOTATE_SEND(int sender, int msgid, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'M',
		INT, pctx->path_id,
		INT, sender,
		INT, msgid,
		INT, size,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		END);
}

void ANNOTATE_RECEIVE(int sender, int msgid, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'm',
		INT, pctx->path_id,
		INT, sender,
		INT, msgid,
		INT, size,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		END);
}

static void output(FILE *fp, ...) {
	char buf[2048], *p=buf+2, len;
	unsigned long n;
	const char *s;
	va_list arg;
	va_start(arg, fp);
	while (1) {
		switch (va_arg(arg, int)) {
			case STRING:
				s = va_arg(arg, const char*);
				len = strlen(s);
				*(p++) = (len>>8) & 0xFF;
				*(p++) = len & 0xFF;
				memcpy(p, s, len);
				p += len;
				break;
			case CHAR:
				*(p++) = va_arg(arg, int) & 0xFF;
				break;
			case INT:
				n = va_arg(arg, unsigned long);
				*(p++) = (n>>24) & 0xFF;
				*(p++) = (n>>16) & 0xFF;
				*(p++) = (n>>8) & 0xFF;
				*(p++) = n & 0xFF;
				break;
			case END:
				goto loop_break;
		}
	}
loop_break:
	va_end(arg);
	len = p-buf;
	buf[0] = (len>>8) & 0xFF;
	buf[1] = len & 0xFF;
	fwrite(buf, len, 1, fp);
	fflush(fp);
}
