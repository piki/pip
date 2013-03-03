#include <assert.h>
#include <byteswap.h>
#include <endian.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include "annotate.h"
#include "socklib.h"

#define BASEPATH "/tmp"
#define MAGIC 0x416e6e6f  // 'Anno'
#define VERSION 3
#define MAXSTACK 10
#define ID(ctx) ((ctx)->idstack[(ctx)->idpos].data)
#define IDLEN(ctx) ((ctx)->idstack[(ctx)->idpos].len)

typedef struct {
	void *data;
	int len;
} PathID;

typedef struct {
	int fd;
	int procfd;
	PathID idstack[MAXSTACK];
	int idpos;
	int log_level;
} ThreadContext;

#include <linux/unistd.h>
#define gettid() syscall(SYS_gettid)

#define USE_PROC 10
#define GETRUSAGE(ru) ({int _n;if(rusage_who==USE_PROC)_n=proc_getrusage(0,ru);else _n=getrusage(rusage_who,ru); _n;})
static int proc_getrusage(int ign, struct rusage *ru);
#ifndef RUSAGE_THREAD
//#warning RUSAGE_THREAD not defined.  Assuming (-3)
#define RUSAGE_THREAD (-3)
#endif

#ifdef THREADS
#include <pthread.h>

static pthread_key_t ctx_key;
#define GET_CTX ({ ThreadContext *_pctx = pthread_getspecific(ctx_key); \
		if (!_pctx) _pctx = new_context(); \
		_pctx; })
static ThreadContext *new_context();
static void free_ctx(void *ctx);

#else  /* no threads */
static ThreadContext ctx;
#define GET_CTX &ctx
#endif
static void gather_header(void);
static void output_header(int fd);

typedef enum { STRING, CHAR, INT, VOIDP, END } OutType;
static void output(int fd, ...);

static char *hostname, *processname;
static const char *basepath;
static char *dest_host;
static unsigned short dest_port;
int annotate_belief_seq = 0;
static int rusage_who;

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
	const char *dest;
	if ((dest = getenv("ANNOTATE_DEST")) == NULL) dest = BASEPATH"/trace";
	if (!strncmp(dest, "tcp:", 4)) {
		char *p = strchr(dest+4, ':');
		assert(p);
		int len = p-(dest+4);
		dest_host = malloc(len+1);
		memcpy(dest_host, dest+4, len);
		dest_host[len] = '\0';
		dest_port = atoi(p+1);

		printf("connecting to \"%s\" %d\n", dest_host, dest_port);
		pctx->fd = sock_connect(dest_host, dest_port);
		if (pctx->fd == -1) exit(1);
	}
	else {
		char fn[256];
		basepath = dest;
		sprintf(fn, "%s-%s-%d", basepath, hostname, getpid());
		pctx->fd = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (pctx->fd == -1) { perror(fn); exit(1); }
		pctx->procfd = -1;
		pctx->idpos = 0;
		ID(pctx) = NULL;
		IDLEN(pctx) = 0;
		const char *lvl = getenv("ANNOTATE_LOG_LEVEL");
		pctx->log_level = lvl ? atoi(lvl) : 255;
	}

#if 0    /* performance test */
	struct timeval tv1, tv2;
	int q;
	struct rusage ru;
	gettimeofday(&tv1, NULL);
	for (q=0; q<100000; q++)
		getrusage(RUSAGE_THREAD, &ru);
	gettimeofday(&tv2, NULL);
	printf("getrusage syscall: %ld\n",
		1000000*(tv2.tv_sec - tv1.tv_sec) + tv2.tv_usec - tv1.tv_usec);
	gettimeofday(&tv1, NULL);
	for (q=0; q<100000; q++)
		proc_getrusage(RUSAGE_THREAD, &ru);
	gettimeofday(&tv2, NULL);
	printf("getrusage in proc: %ld\n",
		1000000*(tv2.tv_sec - tv1.tv_sec) + tv2.tv_usec - tv1.tv_usec);
#endif

	output_header(pctx->fd);

	/* depending on the kernel version, set rusage_who */
	struct utsname ver;
	uname(&ver);
	char *p = strtok(ver.release, ".");
	int major = p ? atoi(p) : 0;
	p = strtok(NULL, ".");
	int minor = p ? atoi(p) : 0;
	p = strtok(NULL, ".");
	int micro = p ? atoi(p) : 0;
	// kernels <= 2.6.5 -> use RUSAGE_SELF
	// kernels > 2.6.5 that have been patched -> use RUSAGE_THREAD
	// stock kernels > 2.6.5 -> use USE_PROC
	if (major < 2) rusage_who = RUSAGE_SELF;
	else if (major > 2) rusage_who = RUSAGE_THREAD;
	else if (minor < 6) rusage_who = RUSAGE_SELF;
	else if (minor > 6) rusage_who = RUSAGE_THREAD;
	else if (micro <= 5) rusage_who = RUSAGE_SELF;
	else if (micro > 5) rusage_who = RUSAGE_THREAD;
	if (rusage_who == RUSAGE_THREAD) {
		struct rusage ru;
		if (getrusage(RUSAGE_THREAD, &ru) == -1)
			rusage_who = USE_PROC;
	}
	fprintf(stderr, "Kernel %d.%d.%d, rusage_who=%d\n",
		major, minor, micro, rusage_who);
}

void ANNOTATE_START_TASK(const char *roles, int level, const char *name) {
	struct rusage ru;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	if (GETRUSAGE(&ru) == -1) { perror("getrusage"); exit(1); }
	output(pctx->fd,
		CHAR, 'T',
		STRING, roles, CHAR, level,
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

void ANNOTATE_END_TASK(const char *roles, int level, const char *name) {
	struct rusage ru;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	if (GETRUSAGE(&ru) == -1) { perror("getrusage"); exit(1); }
	output(pctx->fd,
		CHAR, 't',
		STRING, roles, CHAR, level,
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

static void path_common(int fd, const char *roles, int level, const void *path_id, int idsz) {
	struct rusage ru;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (GETRUSAGE(&ru) == -1) { perror("getrusage"); exit(1); }
	output(fd,
		CHAR, 'P',
		STRING, roles, CHAR, level,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, ru.ru_utime.tv_sec, INT, ru.ru_utime.tv_usec,
		INT, ru.ru_stime.tv_sec, INT, ru.ru_stime.tv_usec,
		INT, ru.ru_minflt,  // minor page faults -- usually process growing
		INT, ru.ru_majflt,  // major page faults -- spin the disk
		INT, ru.ru_nvcsw,   // voluntary context switches -- block on something
		INT, ru.ru_nivcsw,  // involuntary context switches -- cpu hog
		VOIDP, path_id, idsz,
		END);
}

void ANNOTATE_SET_PATH_ID(const char *roles, int level, const void *path_id, int idsz) {
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;

	if (IDLEN(pctx) == idsz && memcmp(path_id, ID(pctx), idsz) == 0) return;  // already set

	path_common(pctx->fd, roles, level, path_id, idsz);

	if (!ID(pctx))
		ID(pctx) = malloc(idsz);
	else if (IDLEN(pctx) != idsz)
		ID(pctx) = realloc(ID(pctx), idsz);
	// else keep the old block as is
	memcpy(ID(pctx), path_id, idsz);
	IDLEN(pctx) = idsz;
}

const void *ANNOTATE_GET_PATH_ID(int *len) {
	ThreadContext *pctx = GET_CTX;
	if (len) *len = IDLEN(pctx);
	return ID(pctx);
}

void ANNOTATE_PUSH_PATH_ID(const char *roles, int level, const void *path_id, int idsz) {
	ThreadContext *pctx = GET_CTX;
	assert(++pctx->idpos < MAXSTACK);
	path_common(pctx->fd, roles, level, path_id, idsz);
	ID(pctx) = malloc(idsz);
	memcpy(ID(pctx), path_id, idsz);
	IDLEN(pctx) = idsz;
}

void ANNOTATE_POP_PATH_ID(const char *roles, int level) {
	ThreadContext *pctx = GET_CTX;
	free(ID(pctx));
	assert(--pctx->idpos >= 0);
	path_common(pctx->fd, roles, level, ID(pctx), IDLEN(pctx));
}

void ANNOTATE_END_PATH_ID(const char *roles, int level, const void *path_id, int idsz) {
	ThreadContext *pctx = GET_CTX;
	assert(ID(pctx));
	if (level > pctx->log_level) return;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	output(pctx->fd,
		CHAR, 'p',
		STRING, roles, CHAR, level,
		INT, tv.tv_sec, INT, tv.tv_usec,
		VOIDP, path_id, idsz,
		END);
	free(ID(pctx));
	ID(pctx) = NULL;
	IDLEN(pctx) = 0;
}

void ANNOTATE_NOTICE(const char *roles, int level, const char *fmt, ...) {
	va_list args;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	char buf[256 - 1 - 9];
	gettimeofday(&tv, NULL);
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	output(pctx->fd,
		CHAR, 'N',
		STRING, roles, CHAR, level,
		INT, tv.tv_sec, INT, tv.tv_usec,
		STRING, buf,
		END);
}

void ANNOTATE_SEND(const char *roles, int level, const void *msgid, int idsz, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	output(pctx->fd,
		CHAR, 'M',
		STRING, roles, CHAR, level,
		VOIDP, msgid, idsz,
		INT, size,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		END);
}

void ANNOTATE_RECEIVE(const char *roles, int level, const void *msgid, int idsz, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	output(pctx->fd,
		CHAR, 'm',
		STRING, roles, CHAR, level,
		VOIDP, msgid, idsz,
		INT, size,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		END);
}

void ANNOTATE_BELIEF_FIRST(int seq, float max_fail_rate, const char *condstr, const char *file, int line) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	output(pctx->fd,
		CHAR, 'B',
		INT, seq,
		INT, (int)(1000000*max_fail_rate),
		STRING, condstr,
		STRING, file,
		INT, line,
		END);
}

void REAL_ANNOTATE_BELIEF(const char *roles, int level, int seq, int condition) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	gettimeofday(&tv, NULL);
	output(pctx->fd,
		CHAR, 'b',
		STRING, roles, CHAR, level,
		INT, tv.tv_sec,
		INT, tv.tv_usec,
		INT, seq,
		CHAR, condition,
		END);
}

#define VSNPRINTF \
	char buf[256]; \
	int len; \
	va_list arg; \
	va_start(arg, fmt); \
	len = vsnprintf(buf, sizeof(buf), fmt, arg); \
	va_end(arg);

/* printf-style helpers for path IDs */
void ANNOTATE_SET_PATH_ID_STR(const char *roles, int level, const char *fmt, ...) {
	VSNPRINTF
	ANNOTATE_SET_PATH_ID(roles, level, buf, len);
}

void ANNOTATE_END_PATH_ID_STR(const char *roles, int level, const char *fmt, ...) {
	VSNPRINTF
	ANNOTATE_END_PATH_ID(roles, level, buf, len);
}

void ANNOTATE_PUSH_PATH_ID_STR(const char *roles, int level, const char *fmt, ...) {
	VSNPRINTF
	ANNOTATE_PUSH_PATH_ID(roles, level, buf, len);
}

void ANNOTATE_SEND_STR(const char *roles, int level, int size, const char *fmt, ...) {
	VSNPRINTF
	ANNOTATE_SEND(roles, level, buf, len, size);
}

void ANNOTATE_RECEIVE_STR(const char *roles, int level, int size, const char *fmt, ...) {
	VSNPRINTF
	ANNOTATE_RECEIVE(roles, level, buf, len, size);
}



static void output(int fd, ...) {
	char buf[2048], *p=buf+2;
	int len;
	unsigned long n;
	const char *s;
	va_list arg;
	va_start(arg, fd);
	while (1) {
		switch (va_arg(arg, int)) {
			case STRING:
				s = va_arg(arg, const char*);
				if (s) {
					len = strlen(s);
					*(p++) = (len>>8) & 0xFF;
					*(p++) = len & 0xFF;
					memcpy(p, s, len);
					p += len;
				}
				else {
					*(p++) = '\0';
					*(p++) = '\0';
				}
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
	write(fd, buf, len);
}

#ifdef THREADS
static ThreadContext *new_context() {
	ThreadContext *pctx = malloc(sizeof(ThreadContext));
	if (dest_host) {
		pctx->fd = sock_connect(dest_host, dest_port);
		if (pctx->fd == -1) exit(1);
	}
	else {
		char fn[256];
		sprintf(fn, "%s-%s-%d-%x", basepath, hostname, getpid(), (int)pthread_self());
		pctx->fd = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (pctx->fd == -1) { perror(fn); exit(1); }
	}
	output_header(pctx->fd);
	pctx->procfd = -1;
	pctx->idpos = 0;
	ID(pctx) = NULL;
	IDLEN(pctx) = 0;
	const char *lvl = getenv("ANNOTATE_LOG_LEVEL");
	pctx->log_level = lvl ? atoi(lvl) : 255;
	pthread_setspecific(ctx_key, pctx);
	return pctx;
}

static void free_ctx(void *ctx) {
	ThreadContext *pctx = ctx;
	if (pctx->fd != -1) close(pctx->fd);
	if (pctx->procfd != -1) close(pctx->procfd);
	free(pctx);
}
#endif

static int proc_getrusage(int ign, struct rusage *ru) {
	ThreadContext *pctx = GET_CTX;
	if (pctx->procfd == -1) {
		char fn[256];
		sprintf(fn, "/proc/%d/task/%d/stat", (int)getpid(), (int)gettid());
		pctx->procfd = open(fn, O_RDONLY);
		if (pctx->procfd == -1) { perror(fn); exit(1); }
	}
	else {
		lseek(pctx->procfd, 0, SEEK_SET);
	}

	char buf[512];
	int len = read(pctx->procfd, buf, sizeof(buf));
	if (len == -1) return -1;
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

static void output_header(int fd) {
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	output(fd,
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
