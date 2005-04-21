#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "events.h"

typedef enum { STRING, VOIDP, CHAR, INT, END } InType;
static int readblock(FILE *_fp, char *buf);
static int scan(const char *buf, ...);

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

static const int printable[96] = {  /* characters 32-127 */
	/* don't print " & ' < > \ #127 */
	1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
};

const char *IDBlock::to_string(void) const {
	static char buf[1024];
	char *p = buf;
	bool inbin = false;
	for (int i=0; i<len; i++) {
		if (isprint(data[i]) && printable[data[i]-' ']) {
			if (inbin) { *(p++) = '}'; inbin = false; }
#if 0
			if (data[i] == '\'') { *(p++) = '\\'; *(p++) = '\''; }
			else if (data[i] == '"') { strcpy(p, "&quot;"); p+=6; }
			else if (data[i] == '&') { strcpy(p, "&amp;"); p+=5; }
			else if (data[i] == '<') { strcpy(p, "&lt;"); p+=5; }
			else if (data[i] == '>') { strcpy(p, "&gt;"); p+=5; }
			else if (data[i] == '\\') { *(p++) = '\\'; *(p++) = '\\'; }
#endif
			else *(p++) = data[i];
		}
		else {
			if (!inbin) { *(p++) = '{'; inbin = true; }
			sprintf(p, "%02x", (unsigned char)data[i]);
			p += 2;
		}
	}
	if (inbin) { *(p++) = '}'; inbin = false; }
	*p = '\0';
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
	assert(version >= 2 && version <= 3);
	// these aren't in Header records
	level = 0;
	roles = NULL;
}
Header::~Header(void) {
	delete[] hostname;
	delete[] processname;
}
void Header::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<header magic=\"%x\" version=\"%d\" host=\"%s\" tv=\"%ld.%06ld\" tz=\"%s%02d%02d\" "
		"pid=\"%d\" tid=\"%d\" ppid=\"%d\" uid=\"%d\" process=\"%s\" />\n",
		2*depth, "", magic, version,
		hostname, tv.tv_sec, tv.tv_usec,
		(tz < 0 ? "+" : "-"), abs(tz)/60, abs(tz)%60,
		pid, tid, ppid, uid, processname);
}

ResourceMark::ResourceMark(int version, const char *buf) {
	if (version >= 3)
		buf += scan(buf, STRING, &roles, CHAR, &level, END);
	else { level = 0; roles = NULL; }

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

Task::Task(int version, const char *buf) : ResourceMark(version, buf), path_id(-1), thread_id(-1) {
	scan(buf+bufsiz(version), STRING, &name, END);
}
Task::~Task(void) { delete[] name; }

void StartTask::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<start_task name=\"%s\" roles=\"%s\" level=%d tv=\"%ld.%06ld\" utime=\"%ld.%06ld\" "
		"stime=\"%ld.%06ld\" minflt=\"%d\" majflt=\"%d\" vcs=\"%d\" ivcs=\"%d\" />\n",
		2*depth, "", name, roles, level, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
		stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
}

void EndTask::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<end_task name=\"%s\" roles=\"%s\" level=%d tv=\"%ld.%06ld\" utime=\"%ld.%06ld\" "
		"stime=\"%ld.%06ld\" minflt=\"%d\" majflt=\"%d\" vcs=\"%d\" ivcs=\"%d\" />\n",
		2*depth, "", name, roles, level, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
		stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
}

NewPathID::NewPathID(int version, const char *buf) : ResourceMark(version, buf) {
	char *idbuf;
	int len;
	scan(buf+bufsiz(version), VOIDP, &idbuf, &len, END);
	path_id.set(idbuf, len);
	delete[] idbuf;
}
void NewPathID::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<new_path_id path_id=\"%s\" roles=\"%s\" level=%d tv=\"%ld.%06ld\" utime=\"%ld.%06ld\" "
		"stime=\"%ld.%06ld\" minflt=\"%d\" majflt=\"%d\" vcs=\"%d\" ivcs=\"%d\" />\n",
		2*depth, "", path_id.to_string(), roles, level, tv.tv_sec, tv.tv_usec, utime.tv_sec, utime.tv_usec,
		stime.tv_sec, stime.tv_usec, minor_fault, major_fault, vol_cs, invol_cs);
}

EndPathID::EndPathID(int version, const char *buf) {
	char *idbuf;
	int len;

	if (version >= 3)
		buf += scan(buf, STRING, &roles, CHAR, &level, END);
	else { level = 0; roles = NULL; }

	scan(buf,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		VOIDP, &idbuf, &len,
		END);
	path_id.set(idbuf, len);
	delete[] idbuf;
}
void EndPathID::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<end_path_id path_id=\"%s\" roles=\"%s\" level=%d tv=\"%ld.%06ld\" />\n",
		2*depth, "", path_id.to_string(), roles, level, tv.tv_sec, tv.tv_usec);
}

Notice::Notice(int version, const char *buf) {
	if (version >= 3)
		buf += scan(buf, STRING, &roles, CHAR, &level, END);
	else { level = 0; roles = NULL; }

	scan(buf,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		STRING, &str,
		END);
}
Notice::~Notice(void) { delete[] str; }
void Notice::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<notice roles=\"%s\" level=%d tv=\"%ld.%06ld\" str=\"%s\" />\n",
		2*depth, "", roles, level, tv.tv_sec, tv.tv_usec, str);
}

Message::Message(int version, const char *buf) {
	char *idbuf;
	int len;

	if (version >= 3)
		buf += scan(buf, STRING, &roles, CHAR, &level, END);
	else { level = 0; roles = NULL; }

	scan(buf,
		VOIDP, &idbuf, &len,
		INT, &size,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		END);
	msgid.set(idbuf, len);
	delete[] idbuf;
}

void MessageSend::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<send msg_id=\"%s\" roles=\"%s\" level=%d size=\"%d\" tv=\"%ld.%06ld\" thread_id=\"%d\" />\n",
		2*depth, "", msgid.to_string(), roles, level, size, tv.tv_sec, tv.tv_usec, thread_id);
}

void MessageRecv::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<recv msg_id=\"%s\" roles=\"%s\" level=%d size=\"%d\" tv=\"%ld.%06ld\" thread_id=\"%d\" />\n",
		2*depth, "", msgid.to_string(), roles, level, size, tv.tv_sec, tv.tv_usec, thread_id);
}

BeliefFirst::BeliefFirst(int version, const char *buf) {
	assert(version >= 3);

	int max_fail_int;
	scan(buf,
		INT, &seq,
		INT, &max_fail_int,
		STRING, &cond,
		STRING, &file,
		INT, &line,
		END);
	max_fail_rate = max_fail_int/1000000.0;

	// these aren't in BeliefFirst records
	tv.tv_sec = tv.tv_usec = level = 0;
	roles = NULL;
}

BeliefFirst::~BeliefFirst(void) { delete[] cond; delete[] file; }

void BeliefFirst::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<belief_first seq=\"%d\" max_fail_rate=\"%.6f\" cond=\"%s\" loc=\"%s:%d\" />\n",
		2*depth, "", seq, max_fail_rate, cond, file, line);
}

Belief::Belief(int version, const char *buf) {
	assert(version >= 3);

	if (version >= 3)
		buf += scan(buf, STRING, &roles, CHAR, &level, END);
	else { level = 0; roles = NULL; }

	char condchar;
	scan(buf,
		INT, &tv.tv_sec, INT, &tv.tv_usec,
		INT, &seq,
		CHAR, &condchar,
		END);
	cond = condchar;
}

void Belief::print(FILE *fp, int depth) {
	fprintf(fp, "%*s<belief seq=\"%d\" cond=\"%s\" roles=\"%s\" level=%d tv=\"%ld.%06ld\" />\n",
		2*depth, "", seq, cond ? "true" : "false", roles, level, tv.tv_sec, tv.tv_usec);
}

static int readblock(FILE *_fp, char *buf) {
	int len = (fgetc(_fp) << 8) + fgetc(_fp);
	if (feof(_fp)) return -1;
	fread(buf, len-2, 1, _fp);
	return len-2;
}

static int scan(const char *buf, ...) {
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
				if (len > 0) {
					*s = new char[len+1];
					memcpy(*s, p, len);
					(*s)[len] = '\0';
					p += len;
				}
				else
					*s = NULL;
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
			default:
				fprintf(stderr, "scan: unknown type %d\n", T);
				exit(1);
		}
	}
loop_break:
	va_end(arg);
	return p-buf;
}

Event *read_event(int version, FILE *_fp) {
	char buf[2048];
	if (readblock(_fp, buf) == -1) return NULL;
	return parse_event(version, buf);
}

Event *parse_event(int version, const char *buf) {
	if (version == -1) assert(buf[0] == 'H');
	switch (buf[0]) {
		case 'H':  return new Header(buf+1);
		case 'T':  return new StartTask(version, buf+1);
		case 't':  return new EndTask(version, buf+1);
		case 'P':  return new NewPathID(version, buf+1);
		case 'p':  return new EndPathID(version, buf+1);
		case 'N':  return new Notice(version, buf+1);
		case 'M':  return new MessageSend(version, buf+1);
		case 'm':  return new MessageRecv(version, buf+1);
		case 'B':  return new BeliefFirst(version, buf+1);
		case 'b':  return new Belief(version, buf+1);
		default:
			fprintf(stderr, "Invalid chunk type '%c' (%d)\n", buf[0], buf[0]);
			return NULL;
	}
}
