#include <assert.h>
#include <byteswap.h>
#include <endian.h>
#include <fcntl.h>
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
#define VERSION 2

typedef struct {
	FILE *fp;
	int procfd;
	void *path_id;
	int path_id_len;
} ThreadContext;

#ifdef THREADS
#include <linux/unistd.h>
#include <pthread.h>

_syscall0(pid_t,gettid)

static pthread_key_t ctx_key;
#define GET_CTX ({ ThreadContext *_pctx = pthread_getspecific(ctx_key); \
		if (!_pctx) _pctx = new_context(); \
		_pctx; })
#define GETRUSAGE proc_getrusage
static int proc_getrusage(int ign, struct rusage *ru);
static ThreadContext *new_context();
static void free_ctx(void *ctx);
static void gather_header(void);
static void output_header(FILE *fp);

#else  /* no threads */
static ThreadContext ctx;
#define GET_CTX &ctx
#define GETRUSAGE getrusage
#endif

typedef enum { STRING, CHAR, INT, VOIDP, END } OutType;
static void output(FILE *fp, ...);

static char *hostname, *processname;

void ANNOTATE_INIT(void) {
#ifdef THREADS
	ThreadContext *pctx = malloc(sizeof(ThreadContext));
	pthread_key_create(&ctx_key, free_ctx);
	pthread_setspecific(ctx_key, pctx);
#else
	ThreadContext *pctx = &ctx;
#endif

	/* prepare values for the header */
	gather_header();

	/* open the output file */
	char fn[256];
	sprintf(fn, BASEPATH"/trace-%s-%d", hostname, getpid());
	pctx->fp = fopen(fn, "w");
	if (!pctx->fp) { perror(fn); exit(1); }
	pctx->procfd = -1;
	pctx->path_id = NULL;
	pctx->path_id_len = 0;

#if 0    /* performance test */
	struct timeval tv1, tv2;
	int q;
	struct rusage ru;
	gettimeofday(&tv1, NULL);
	for (q=0; q<100000; q++)
		getrusage(RUSAGE_SELF, &ru);
	gettimeofday(&tv2, NULL);
	printf("getrusage syscall: %ld\n",
		1000000*(tv2.tv_sec - tv1.tv_sec) + tv2.tv_usec - tv1.tv_usec);
	gettimeofday(&tv1, NULL);
	for (q=0; q<100000; q++)
		proc_getrusage(RUSAGE_SELF, &ru);
	gettimeofday(&tv2, NULL);
	printf("getrusage in proc: %ld\n",
		1000000*(tv2.tv_sec - tv1.tv_sec) + tv2.tv_usec - tv1.tv_usec);
#endif

	output_header(pctx->fp);
}

void ANNOTATE_START_TASK(const char *name) {
	struct rusage ru;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	assert(pctx->path_id);
	gettimeofday(&tv, NULL);
	GETRUSAGE(RUSAGE_SELF, &ru);
	output(pctx->fp,
		CHAR, 'T',
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
	assert(pctx->path_id);
	gettimeofday(&tv, NULL);
	GETRUSAGE(RUSAGE_SELF, &ru);
	output(pctx->fp,
		CHAR, 't',
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

void ANNOTATE_SET_PATH_ID(const void *path_id, int idsz) {
	ThreadContext *pctx = GET_CTX;

	struct rusage ru;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	GETRUSAGE(RUSAGE_SELF, &ru);
	output(pctx->fp,
		CHAR, 'P',
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, ru.ru_utime.tv_sec, INT, ru.ru_utime.tv_usec,
		INT, ru.ru_stime.tv_sec, INT, ru.ru_stime.tv_usec,
		INT, ru.ru_minflt,  // minor page faults -- usually process growing
		INT, ru.ru_majflt,  // major page faults -- spin the disk
		INT, ru.ru_nvcsw,   // voluntary context switches -- block on something
		INT, ru.ru_nivcsw,  // involuntary context switches -- cpu hog
		VOIDP, path_id, idsz,
		END);

	if (pctx->path_id) free(pctx->path_id);
	pctx->path_id = malloc(idsz);
	memcpy(pctx->path_id, path_id, idsz);
	pctx->path_id_len = idsz;
}

const void *ANNOTATE_GET_PATH_ID(int *len) {
	ThreadContext *pctx = GET_CTX;
	if (len) *len = pctx->path_id_len;
	return pctx->path_id;
}

void ANNOTATE_END_PATH_ID(const void *path_id, int idsz) {
	ThreadContext *pctx = GET_CTX;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'p',
		INT, tv.tv_sec, INT, tv.tv_usec,
		VOIDP, path_id, idsz,
		END);
	if (pctx->path_id) {
		free(pctx->path_id);
		pctx->path_id = NULL;
		pctx->path_id_len = 0;
	}
}

void ANNOTATE_NOTICE(const char *fmt, ...) {
	va_list args;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	assert(pctx->path_id);
	char buf[256 - 1 - 9];
	gettimeofday(&tv, NULL);
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	output(pctx->fp,
		CHAR, 'N',
		INT, tv.tv_sec, INT, tv.tv_usec,
		STRING, buf,
		END);
}

void ANNOTATE_SEND(const void *msgid, int idsz, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	assert(pctx->path_id);
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'M',
		VOIDP, msgid, idsz,
		INT, size,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		END);
}

void ANNOTATE_RECEIVE(const void *msgid, int idsz, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	assert(pctx->path_id);
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'm',
		VOIDP, msgid, idsz,
		INT, size,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		END);
}

#if 0
void REAL_ANNOTATE_BELIEF(int condition, float max_fail_rate, const char *condstr, const char *file, int line) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	output(pctx->fp,
		CHAR, 'B',
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		CHAR, condition,
		INT, 1000000*max_fail_rate,
		STRING, condstr,
		STRING, file,
		INT, line,
		END);
}
#endif

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
			case VOIDP:
				s = va_arg(arg, const char*);
				len = va_arg(arg, int);
				*(p++) = (len & 0xFF);
				memcpy(p, s, len);
				p += len;
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

#ifdef THREADS
static ThreadContext *new_context() {
	ThreadContext *pctx = malloc(sizeof(ThreadContext));
	char fn[256];
	sprintf(fn, BASEPATH"/trace-%s-%d-%x", hostname, getpid(), (int)pthread_self());
	pctx->fp = fopen(fn, "w");
	if (!pctx->fp) { perror(fn); exit(1); }
	output_header(pctx->fp);
	pctx->procfd = -1;
	pctx->path_id = NULL;
	pctx->path_id_len = 0;
	pthread_setspecific(ctx_key, pctx);
	return pctx;
}

static void free_ctx(void *ctx) {
	ThreadContext *pctx = ctx;
	if (pctx->fp) fclose(pctx->fp);
	if (pctx->procfd != -1) close(pctx->procfd);
	free(pctx);
}

static int proc_getrusage(int ign, struct rusage *ru) {
	ThreadContext *pctx = GET_CTX;
	if (pctx->procfd == -1) {
		char fn[256];
		sprintf(fn, "/proc/%d/task/%d/stat", getpid(), gettid());
		pctx->procfd = open(fn, O_RDONLY);
		if (pctx->procfd == -1) { perror(fn); exit(1); }
	}
	else {
		lseek(pctx->procfd, 0, SEEK_SET);
	}

	char buf[512];
	int len = read(pctx->procfd, buf, sizeof(buf));
	if (len == -1) { perror("read"); return -1; }
	buf[len] = '\0';
	int skip, tmp;
	const char *p = buf;
	for (skip=9; skip>0 && *p; p++) if (*p == ' ') skip--;
	ru->ru_minflt = atoi(p);
	for (skip=2; skip>0 && *p; p++) if (*p == ' ') skip--;
	ru->ru_majflt = atoi(p);
	for (skip=2; skip>0 && *p; p++) if (*p == ' ') skip--;
	tmp = atoi(p);  /* utime in jiffies */
	ru->ru_utime.tv_sec = tmp / 100;
	ru->ru_utime.tv_usec = (tmp % 100) * 10000;
	for (skip=1; skip>0 && *p; p++) if (*p == ' ') skip--;
	tmp = atoi(p);  /* utime in jiffies */
	ru->ru_stime.tv_sec = tmp / 100;
	ru->ru_stime.tv_usec = (tmp % 100) * 10000;
	ru->ru_nvcsw = ru->ru_nivcsw = 0;   /* proc doesn't have these? */

	return 0;
}
#endif

static void gather_header(void) {
	char buf[1024];
	FILE *inp;
	gethostname(buf, sizeof(buf));
	hostname = strdup(buf);
#ifdef linux
	inp = fopen("/proc/self/status", "r");  /* Linuxism! */
	if (inp) {
		while (fgets(buf, sizeof(buf), inp)) {
			if (!strncasecmp(buf, "Name:", 5)) {
				char *p;
				p = strchr(buf, '\n');
				*p = '\0';
				p = strchr(buf, ':');
				++p;
				while (*p == ' ' || *p == '\t')  p++;
				processname = strdup(p);
				break;
			}
		}
		fclose(inp);
	}
#else
#warning The process name will not be present on non-Linux platforms
#endif
}

static void output_header(FILE *fp) {
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	output(fp,
		CHAR, 'H',
		INT, MAGIC,
		INT, VERSION,
		STRING, hostname,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, tz.tz_minuteswest,
		INT, getpid(),
		INT, gettid(),
		INT, getppid(),
		INT, getuid(),
		STRING, processname ? processname : "",
		END);
}
