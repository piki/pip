/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

#define NO_ZLIB  /* doesn't work yet.  doesn't flush on unclean exit. */
#include <assert.h>
#include <byteswap.h>
#include <endian.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef NO_ZLIB
#include <zlib.h>
#endif
#include <sys/resource.h>
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

typedef union {
	int fd;
	FILE *fp;
#ifndef NO_ZLIB
	gzFile zfp;
#endif
} OutputPath;
enum {
	OP_FD,
	OP_STDIO,
#ifndef NO_ZLIB
	OP_ZLIB
#endif
} output_type = OP_FD;

typedef struct {
	void *data;
	int len;
} PathID;

typedef struct {
	OutputPath outp;
#ifdef THREADS
	int procfd;
#endif
	PathID idstack[MAXSTACK];
	int idpos;
	int log_level;
} ThreadContext;

#ifdef THREADS
#include <linux/unistd.h>
#define gettid() syscall(__NR_gettid)
#define USE_PROC 10
#define GETRUSAGE(ru) ({int _n;if(rusage_who==USE_PROC)_n=proc_getrusage(0,ru);else _n=getrusage(rusage_who,ru); _n;})
static int proc_getrusage(int ign, struct rusage *ru);
#ifndef RUSAGE_THREAD
//#warning RUSAGE_THREAD not defined.  Assuming (-3)
#define RUSAGE_THREAD (-3)
#endif

#include <pthread.h>

static pthread_key_t ctx_key;
#define GET_CTX ({ ThreadContext *_pctx = pthread_getspecific(ctx_key); \
		if (!_pctx) _pctx = new_context(); \
		_pctx; })
static ThreadContext *new_context();
static void free_ctx(void *ctx);
static int rusage_who;

#else  /* no threads */
static ThreadContext ctx;
#define GETRUSAGE(ru) getrusage(RUSAGE_SELF,ru)
#define GET_CTX (&ctx)
int gettid() { return getpid(); }
#endif

static void gather_header(void);
static void output_header(OutputPath outp);
#ifndef NO_ZLIB
static void pip_cleanup(void);
#endif

typedef enum { STRING, CHAR, INT, VOIDP, TIME, END } OutType;
static void output(OutputPath outp, ...);
static OutputPath new_output(int is_sub_thread);

static char *hostname, *processname;
static const char *basepath;
static char *dest_host;
static unsigned short dest_port;
int annotate_belief_seq = 0;
static int my_pid;   /* in case of old kernels where getpid() doesn't work with threads */

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

	/* parse the destination host:port, if any */
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

		fprintf(stderr, "Pip connecting to \"%s\" %d\n", dest_host, dest_port);
	}
	else
		basepath = dest;

	/* use fd, stdio, or zlib writes for trace files? */
	const char *mode;
	if ((mode = getenv("ANNOTATE_WRITE_MODE")) != NULL) {
		if (!strcasecmp(mode, "stdio")) output_type = OP_STDIO;
#ifndef NO_ZLIB
		else if (!strcasecmp(mode, "zlib")) output_type = OP_ZLIB;
#endif
		else if (!strcasecmp(mode, "fd")) output_type = OP_FD;
		else {
			fprintf(stderr, "Invalid write mode: \"%s\"\n", mode);
			exit(1);
		}
	}
	else output_type = OP_FD;
	
	/* open the output file */
	pctx->outp = new_output(0);

#ifdef THREADS
	pctx->procfd = -1;
#endif
	pctx->idpos = 0;
	ID(pctx) = NULL;
	IDLEN(pctx) = 0;
	const char *lvl = getenv("ANNOTATE_LOG_LEVEL");
	pctx->log_level = lvl ? atoi(lvl) : 255;

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

	output_header(pctx->outp);

#ifdef THREADS
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

	fprintf(stderr, "Pip starting.  Kernel %d.%d.%d, rusage_who=%d (",
		major, minor, micro, rusage_who);
	switch (rusage_who) {
		case RUSAGE_THREAD:  fprintf(stderr, "RUSAGE_THREAD)\n");  break;
		case RUSAGE_SELF:    fprintf(stderr, "RUSAGE_SELF)\n");    break;
		case USE_PROC:       fprintf(stderr, "USE_PROC)\n");       break;
		default:             fprintf(stderr, "unknown)\n");
	}

#else
	fprintf(stderr, "Pip starting in thread-oblivious mode.\n");
#endif

#ifndef NO_ZLIB
	atexit(pip_cleanup);
#endif
}

void ANNOTATE_START_TASK(const char *roles, int level, const char *name) {
	struct rusage ru;
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	if (GETRUSAGE(&ru) == -1) { perror("getrusage"); exit(1); }
	output(pctx->outp,
		CHAR, 'T',
		STRING, roles, CHAR, level,
		TIME, &tv,
		TIME, &ru.ru_utime,
		TIME, &ru.ru_stime,
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
	output(pctx->outp,
		CHAR, 't',
		STRING, roles, CHAR, level,
		TIME, &tv,
		TIME, &ru.ru_utime,
		TIME, &ru.ru_stime,
		INT, ru.ru_minflt,  // minor page faults -- usually process growing
		INT, ru.ru_majflt,  // major page faults -- spin the disk
		INT, ru.ru_nvcsw,   // voluntary context switches -- block on something
		INT, ru.ru_nivcsw,  // involuntary context switches -- cpu hog
		STRING, name,
		END);
}

static void path_common(OutputPath outp, const char *roles, int level, const void *path_id, int idsz) {
	struct rusage ru;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (GETRUSAGE(&ru) == -1) { perror("getrusage"); exit(1); }
	output(outp,
		CHAR, 'P',
		STRING, roles, CHAR, level,
		TIME, &tv,
		TIME, &ru.ru_utime,
		TIME, &ru.ru_stime,
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

	path_common(pctx->outp, roles, level, path_id, idsz);

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
	path_common(pctx->outp, roles, level, path_id, idsz);
	ID(pctx) = malloc(idsz);
	memcpy(ID(pctx), path_id, idsz);
	IDLEN(pctx) = idsz;
}

void ANNOTATE_POP_PATH_ID(const char *roles, int level) {
	ThreadContext *pctx = GET_CTX;
	free(ID(pctx));
	assert(--pctx->idpos >= 0);
	path_common(pctx->outp, roles, level, ID(pctx), IDLEN(pctx));
}

void ANNOTATE_END_PATH_ID(const char *roles, int level, const void *path_id, int idsz) {
	ThreadContext *pctx = GET_CTX;
	assert(ID(pctx));
	if (level > pctx->log_level) return;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	output(pctx->outp,
		CHAR, 'p',
		STRING, roles, CHAR, level,
		TIME, &tv,
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
	output(pctx->outp,
		CHAR, 'N',
		STRING, roles, CHAR, level,
		TIME, &tv,
		STRING, buf,
		END);
}

void ANNOTATE_SEND(const char *roles, int level, const void *msgid, int idsz, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	output(pctx->outp,
		CHAR, 'M',
		STRING, roles, CHAR, level,
		VOIDP, msgid, idsz,
		INT, size,
		TIME, &tv,
		END);
}

void ANNOTATE_RECEIVE(const char *roles, int level, const void *msgid, int idsz, int size) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	if (level > pctx->log_level) return;
	assert(ID(pctx));
	gettimeofday(&tv, NULL);
	output(pctx->outp,
		CHAR, 'm',
		STRING, roles, CHAR, level,
		VOIDP, msgid, idsz,
		INT, size,
		TIME, &tv,
		END);
}

void ANNOTATE_BELIEF_FIRST(int seq, float max_fail_rate, const char *condstr, const char *file, int line) {
	struct timeval tv;
	ThreadContext *pctx = GET_CTX;
	gettimeofday(&tv, NULL);
	output(pctx->outp,
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
	output(pctx->outp,
		CHAR, 'b',
		STRING, roles, CHAR, level,
		TIME, &tv,
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



static void output(OutputPath outp, ...) {
	char buf[2048], *p=buf+2;
	int len;
	unsigned long n;
	const char *s;
	struct timeval *tv;
	va_list arg;
	va_start(arg, outp);
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
			case TIME:
				tv = va_arg(arg, struct timeval *);
				*(p++) = (tv->tv_sec>>24) & 0xFF;
				*(p++) = (tv->tv_sec>>16) & 0xFF;
				*(p++) = (tv->tv_sec>>8) & 0xFF;
				*(p++) = tv->tv_sec & 0xFF;
				*(p++) = (tv->tv_usec>>24) & 0xFF;
				*(p++) = (tv->tv_usec>>16) & 0xFF;
				*(p++) = (tv->tv_usec>>8) & 0xFF;
				*(p++) = tv->tv_usec & 0xFF;
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
	int result;
	switch (output_type) {
		case OP_FD:     result = write(outp.fd, buf, len);      break;
		case OP_STDIO:  result = fwrite(buf, 1, len, outp.fp);  break;
#ifndef NO_ZLIB
		case OP_ZLIB:   result = gzwrite(outp.zfp, buf, len);   break;
#endif
	}
}

#ifdef THREADS
static ThreadContext *new_context() {
	ThreadContext *pctx = malloc(sizeof(ThreadContext));
	pctx->outp = new_output(1);
	output_header(pctx->outp);
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
	fprintf(stderr, "Pip ending one thread.\n");
	switch (output_type) {
		case OP_FD:     if (pctx->outp.fd != -1) close(pctx->outp.fd);        break;
		case OP_STDIO:  if (pctx->outp.fp != NULL) fclose(pctx->outp.fp);     break;
#ifndef NO_ZLIB
		case OP_ZLIB:   if (pctx->outp.zfp != NULL) gzclose(pctx->outp.zfp);  break;
#endif
	}
	if (pctx->procfd != -1) close(pctx->procfd);
	free(pctx);
}

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
#endif  /* threads */

static void gather_header(void) {
	char buf[1024];
	FILE *inp;
	const char* hname;

	hname = getenv("ANNOTATE_HOSTNAME");
	if (hname == NULL) {
		gethostname(buf, sizeof(buf));
		hostname = strdup(buf);
	}
	else
		hostname = strdup(hname);

	my_pid = getpid();
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
	sprintf(buf, "ps -o command %d", getpid());
	inp = popen(buf, "r");
	if (inp) {
		fgets(buf, sizeof(buf), inp);   /* header */
		fgets(buf, sizeof(buf), inp);   /* content */
		*strchr(buf, '\n') = '\0';
		processname = strdup(buf);
		pclose(inp);
	}
#endif
}

static void output_header(OutputPath outp) {
	struct timeval tv;
	struct timezone tz;
	gettimeofday(&tv, &tz);
	output(outp,
		CHAR, 'H',
		INT, MAGIC,
		INT, VERSION,
		STRING, hostname,
		INT, tv.tv_sec, INT, tv.tv_usec,
		INT, tz.tz_minuteswest,
		INT, my_pid,
		INT, gettid(),
		INT, getppid(),
		INT, getuid(),
		STRING, processname ? processname : "",
		END);
}

static OutputPath new_output(int is_sub_thread) {
	OutputPath ret;

	if (dest_host) {
		int fd = sock_connect(dest_host, dest_port);
		if (fd == -1) exit(1);
		switch (output_type) {
			case OP_FD:
				ret.fd = fd;
				break;
			case OP_STDIO:
				ret.fp = fdopen(fd, "w");
				if (ret.fp == NULL) { perror("fdopen"); exit(1); }
				break;
#ifndef NO_ZLIB
			case OP_ZLIB:
				ret.zfp = gzdopen(fd, "wb3");
				if (ret.zfp == NULL) { perror("gzdopen"); exit(1); }
				break;
#endif
		}
	}
	else {
		char fn[256];
#ifdef THREADS
		if (is_sub_thread)
			sprintf(fn, "%s-%s-%d-%x", basepath, hostname, my_pid, (int)pthread_self());
		else
#endif
			sprintf(fn, "%s-%s-%d", basepath, hostname, my_pid);
		switch (output_type) {
			case OP_FD:
				ret.fd = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0644);
				if (ret.fd == -1) { perror(fn); exit(1); }
				break;
			case OP_STDIO:
				ret.fp = fopen(fn, "w");
				if (ret.fp == NULL) { perror(fn); exit(1); }
				break;
#ifndef NO_ZLIB
			case OP_ZLIB:
				strcat(fn, ".gz");
				ret.zfp = gzopen(fn, "wb3");
				if (ret.zfp == NULL) { perror("gzopen"); exit(1); }
				break;
#endif
		}
	}

	return ret;
}

#ifndef NO_ZLIB
static void pip_cleanup(void) {
	fprintf(stderr, "Pip cleanup.\n");
	if (output_type == OP_ZLIB) gzclose(GET_CTX->outp.zfp);
}
#endif
