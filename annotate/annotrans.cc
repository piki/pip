#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef enum { STRING, CHAR, INT, END } InType;
static int readblock(FILE *_fp, char *buf);
static void scan(const char *buf, ...);

class Event {
public:
	virtual void print(FILE *fp = stdin) = 0;
	virtual ~Event(void) {}
};
Event *read_event(FILE *_fp);

class Header : public Event {
public:
	Header(const char *buf) {
		scan(buf,
			INT, &magic,
			INT, &version,
			STRING, &hostname,
			INT, &tv.tv_sec, INT, &tv.tv_usec,
			INT, &tz,
			INT, &pid,
			INT, &ppid,
			INT, &uid,
			STRING, &processname,
			END);
	}
	~Header(void) {
		delete[] hostname;
		delete[] processname;
	}
	virtual void print(FILE *fp) {
		printf("HEADER: magic=%x version=%d host=\"%s\" tv=%ld.%06ld tz=%d "
			"pid=%d ppid=%d uid=%d process=\"%s\"\n", magic, version,
			hostname, tv.tv_sec, tv.tv_usec, tz, pid, ppid, uid, processname);
	}
	int magic, version;
	struct timeval tv;
	int tz, pid, ppid, uid;
	char *hostname, *processname;
};

class ResourceMark : public Event {
public:
	ResourceMark(const char *buf) {
		scan(buf,
			INT, &path_id,
			INT, &tv.tv_sec, INT, &tv.tv_usec,
			INT, &utime.tv_sec, INT, &utime.tv_usec,
			INT, &stime.tv_sec, INT, &stime.tv_usec,
			INT, &minor_fault,
			INT, &major_fault,
			INT, &vol_cs,
			INT, &invol_cs,
			END);
	}
	static const int bufsiz = 11*4;  // 11 ints
	int path_id;
	int minor_fault, major_fault, vol_cs, invol_cs;
	struct timeval tv, utime, stime;
};

class StartTask : public ResourceMark {
public:
	StartTask(const char *buf) : ResourceMark(buf) {
		scan(buf+ResourceMark::bufsiz, STRING, &name, END);
	}
	~StartTask(void) { delete[] name; }
	virtual void print(FILE *fp) {
		printf("START_TASK: name=\"%s\" path_id=%d tv=%ld.%06ld utime=%ld.%06ld "
			"stime=%ld.%06ld minflt=%d majflt=%d vcs=%d ivcs=%d\n",
			name, path_id, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
			stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
	}
	char *name;
};

class EndTask : public ResourceMark {
public:
	EndTask(const char *buf) : ResourceMark(buf) {
		scan(buf+ResourceMark::bufsiz, STRING, &name, END);
	}
	~EndTask(void) { delete[] name; }
	virtual void print(FILE *fp) {
		printf("END_TASK: name=\"%s\" path_id=%d tv=%ld.%06ld utime=%ld.%06ld "
			"stime=%ld.%06ld minflt=%d majflt=%d vcs=%d ivcs=%d\n",
			name, path_id, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
			stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
	}
	char *name;
};

class NewPathID : public ResourceMark {
public:
	NewPathID(const char *buf) : ResourceMark(buf) { }
	virtual void print(FILE *fp) {
		printf("NEW_PATH_ID: path_id=%d tv=%ld.%06ld utime=%ld.%06ld "
			"stime=%ld.%06ld minflt=%d majflt=%d vcs=%d ivcs=%d\n",
			path_id, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
			stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
	}
};

class EndPathID : public Event {
public:
	EndPathID(const char *buf) {
		scan(buf,
			INT, &path_id,
			INT, &tv.tv_sec, INT, &tv.tv_usec,
			END);
	}
	virtual void print(FILE *fp) {
		printf("END_PATH_ID: path_id=%d tv=%ld.%06ld\n",
			path_id, tv.tv_sec, tv.tv_usec);
	}
	int path_id;
	struct timeval tv;
};

class Notice : public Event {
public:
	Notice(const char *buf) {
		scan(buf,
			INT, &path_id,
			INT, &tv.tv_sec, INT, &tv.tv_usec,
			STRING, &str,
			END);
	}
	~Notice(void) { delete[] str; }
	virtual void print(FILE *fp) {
		printf("NOTICE: path_id=%d tv=%ld.%06ld str=\"%s\"\n",
			path_id, tv.tv_sec, tv.tv_usec, str);
	}
	int path_id;
	struct timeval tv;
	char *str;
};

class Message : public Event {
public:
	Message(const char *buf) {
		scan(buf,
			INT, &path_id,
			INT, &sender,
			INT, &msg_id,
			INT, &size,
			INT, &tv.tv_sec, INT, &tv.tv_usec,
			END);
	}
	int path_id, sender, msg_id, size;
	struct timeval tv;
};

class MessageSend : public Message {
public:
	MessageSend(const char *buf) : Message(buf) { }
	virtual void print(FILE *fp) {
		printf("SEND: path_id=%d sender=%d msg_id=%d size=%d tv=%ld.%06ld\n",
			path_id, sender, msg_id, size, tv.tv_sec, tv.tv_usec);
	}
};

class MessageRecv : public Message {
public:
	MessageRecv(const char *buf) : Message(buf) { }
	virtual void print(FILE *fp) {
		printf("RECV: path_id=%d sender=%d msg_id=%d size=%d tv=%ld.%06ld\n",
			path_id, sender, msg_id, size, tv.tv_sec, tv.tv_usec);
	}
};

static int readblock(FILE *_fp, char *buf) {
	int len = (fgetc(_fp) << 8) + fgetc(_fp);
	if (feof(_fp)) return -1;
	fread(buf, len-2, 1, _fp);
	return len-2;
}

static void scan(const char *buf, ...) {
	const char *p=buf;
	char **s;
	char *c;
	int *ip, len;
	va_list arg;
	va_start(arg, buf);
	while (1) {
		int T = va_arg(arg, int);
		switch (T) {
			case STRING:
				len = (*p << 8) + *(p+1);  p+=2;
				s = va_arg(arg, char**);
				*s = new char[len+1];
				memcpy(*s, p, len);
				(*s)[len] = '\0';
				p += len;
				break;
			case CHAR:
				c = va_arg(arg, char*);
				*c = *(p++);
				break;
			case INT:
				ip = va_arg(arg, int*);
				*ip = ((p[0] << 24) & 0xFF000000)
					| ((p[1] << 16) & 0xFF0000)
					| ((p[2] << 8) & 0xFF00)
					| (p[3] & 0xFF);
				p += 4;
				break;
			case END:
				goto loop_break;
		}
	}
loop_break:
	va_end(arg);
}

Event *read_event(FILE *_fp) {
	char buf[2048];
	if (readblock(_fp, buf) == -1) return NULL;
	switch (buf[0]) {
		case 'H':  return new Header(buf+1);
		case 'T':  return new StartTask(buf+1);
		case 't':  return new EndTask(buf+1);
		case 'P':  return new NewPathID(buf+1);
		case 'p':  return new EndPathID(buf+1);
		case 'N':  return new Notice(buf+1);
		case 'M':  return new MessageSend(buf+1);
		case 'm':  return new MessageRecv(buf+1);
		default:
			fprintf(stderr, "Invalid chunk type '%c' (%d)\n", buf[0], buf[0]);
			return NULL;
	}
}

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
