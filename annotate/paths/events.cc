#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "events.h"

typedef enum { STRING, VOIDP, CHAR, INT, END } InType;
static int readblock(FILE *_fp, char *buf);
static void scan(const char *buf, ...);

void IDBlock::set(char *_data, int _len) {
	if (data) delete[] data;
	len = _len;
	data = new char[len];
	memcpy(data, _data, len);
}

bool IDBlock::operator==(const IDBlock &test) const {
	return len == test.len && !memcmp(data, test.data, len);
}

bool IDBlock::operator< (const IDBlock &test) const {
	if (len < test.len) return true;
	if (len > test.len) return false;
	return memcmp(data, test.data, len) < 0;
}

const char *IDBlock::to_string(void) const {
	static char buf[512];
	for (int i=0; i<len; i++)
		sprintf(&buf[i*2], "%02x", (unsigned char)data[i]);
	buf[len*2] = '\0';
	return buf;
}

Header::Header(const char *buf) {
	scan(buf,
		INT, &magic,
		INT, &version,
		STRING, &hostname,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		INT, &tz,
		INT, &pid,
		INT, &tid,
		INT, &ppid,
		INT, &uid,
		STRING, &processname,
		END);
	assert(version == 2);
}
Header::~Header(void) {
	delete[] hostname;
	delete[] processname;
}
void Header::print(FILE *fp) {
	fprintf(fp, "HEADER: magic=%x version=%d host=\"%s\" tv=%ld.%06ld tz=%d "
		"pid=%d tid=%d ppid=%d uid=%d process=\"%s\"\n", magic, version,
		hostname, tv.tv_sec, tv.tv_usec, tz, pid, tid, ppid, uid, processname);
}

ResourceMark::ResourceMark(const char *buf) {
	scan(buf,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		INT, &utime.tv_sec, INT, &utime.tv_usec,
		INT, &stime.tv_sec, INT, &stime.tv_usec,
		INT, &minor_fault,
		INT, &major_fault,
		INT, &vol_cs,
		INT, &invol_cs,
		END);
}

Task::Task(const char *buf) : ResourceMark(buf), path_id(-1), thread_id(-1) {
	scan(buf+ResourceMark::bufsiz, STRING, &name, END);
}
Task::~Task(void) { delete[] name; }

void StartTask::print(FILE *fp) {
	fprintf(fp, "START_TASK: name=\"%s\" tv=%ld.%06ld utime=%ld.%06ld "
		"stime=%ld.%06ld minflt=%d majflt=%d vcs=%d ivcs=%d\n",
		name, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
		stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
}

void EndTask::print(FILE *fp) {
	fprintf(fp, "END_TASK: name=\"%s\" tv=%ld.%06ld utime=%ld.%06ld "
		"stime=%ld.%06ld minflt=%d majflt=%d vcs=%d ivcs=%d\n",
		name, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
		stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
}

NewPathID::NewPathID(const char *buf) : ResourceMark(buf) {
	char *idbuf;
	int len;
	scan(buf+ResourceMark::bufsiz, VOIDP, &idbuf, &len, END);
	path_id.set(idbuf, len);
	delete[] idbuf;
}
void NewPathID::print(FILE *fp) {
	fprintf(fp, "NEW_PATH_ID: path_id=%s tv=%ld.%06ld utime=%ld.%06ld "
		"stime=%ld.%06ld minflt=%d majflt=%d vcs=%d ivcs=%d\n",
		path_id.to_string(), tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
		stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
}

EndPathID::EndPathID(const char *buf) {
	char *idbuf;
	int len;
	scan(buf,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		VOIDP, &idbuf, &len,
		END);
	path_id.set(idbuf, len);
	delete[] idbuf;
}
void EndPathID::print(FILE *fp) {
	fprintf(fp, "END_PATH_ID: path_id=%s tv=%ld.%06ld\n",
		path_id.to_string(), tv.tv_sec, tv.tv_usec);
}

Notice::Notice(const char *buf) {
	scan(buf,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		STRING, &str,
		END);
}
Notice::~Notice(void) { delete[] str; }
void Notice::print(FILE *fp) {
	fprintf(fp, "NOTICE: tv=%ld.%06ld str=\"%s\"\n",
		tv.tv_sec, tv.tv_usec, str);
}

Message::Message(const char *buf) {
	char *idbuf;
	int len;
	scan(buf,
		VOIDP, &idbuf, &len,
		INT, &size,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		END);
	msgid.set(idbuf, len);
	delete[] idbuf;
}

void MessageSend::print(FILE *fp) {
	fprintf(fp, "SEND: msg_id=%s size=%d tv=%ld.%06ld thread_id=%d\n",
		msgid.to_string(), size, tv.tv_sec, tv.tv_usec, thread_id);
}

void MessageRecv::print(FILE *fp) {
	fprintf(fp, "RECV: msg_id=%s size=%d tv=%ld.%06ld thread_id=%d\n",
		msgid.to_string(), size, tv.tv_sec, tv.tv_usec, thread_id);
}

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
	int *ip, len, *lenp;
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
			case VOIDP:
				s = va_arg(arg, char**);
				lenp = va_arg(arg, int*);
				len = *lenp = *(p++);
				*s = new char[len];
				memcpy(*s, p, len);
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
