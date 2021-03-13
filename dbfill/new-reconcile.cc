/*
 * Copyright (c) 2007 Patrick Reynolds.  All rights reserved.
 * Please see COPYING for license terms.
 */

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <map>
#include <vector>
#include "events.h"
#include "pipdb.h"

#define PIPDB_VERSION 1

#if 0
static void safe_seek(FILE *fp, int ofs, int whence) {
	fprintf(stderr, "seek: %d %d\n", ofs, whence);
	if (whence == SEEK_SET) {
		assert(ofs >= 0);
		assert(ofs < 5000000);
	}
	else if (whence == SEEK_CUR) {
		assert(ofs > -50000);
		assert(ofs < 50000);
	}
	fseek(fp, ofs, whence);
}
#define fseek safe_seek
#endif

struct ltstr {
  bool operator()(const char* s1, const char* s2) const { return strcmp(s1, s2) < 0; }
};
typedef std::vector<StartTask *> StartList;
typedef std::map<const char *, StartList, ltstr> NameTaskMap;
typedef std::map<std::string, Message*> MessageMap;
struct Path {
	Path(void) { tasks = notices = messages = 0; }
	int tasks, notices, messages;
	NameTaskMap start_task;    // mapping from task name to stack of task-start events
	std::map<std::string, std::vector<Task*> > unpaired_tasks;
	int total(void) const { return tasks + notices + messages; }
};
struct TaskEnt {
	int tasks, name_ofs;
	TaskEnt(void) : tasks(0), name_ofs(0) {}
};

static void usage(const char *prog);
static void first_pass(FILE *outp, const char *fn);
static void second_pass(FILE *outp, const char *fn, int thread_id);
static void pipdb_write_task_index(FILE *outp);
static void pipdb_write_path_index(FILE *outp);
static void pipdb_write_thread(FILE *outp, Header *hdr);
static int pipdb_task_length(StartTask *start, EndTask *end);
static int pipdb_notice_length(Notice *notice);
static int pipdb_message_length(Message *send, Message *recv);
static void pipdb_write_task(FILE *outp, Task *start, Task *end, Path *current_path);
static void pipdb_write_notice(FILE *outp, Notice *notice, int thread_id, Path *current_path);
static void pipdb_write_message(FILE *outp, Message *send, Message *recv, Path *current_path);
bool handle_end_task(FILE *outp, Task *end, Path *path);
static void reconcile(FILE *outp, Message *send, Message *recv, bool is_send, int thread_id, Path *path);
static void check_unpaired_tasks(FILE *outp);
static void check_unpaired_messages(FILE *outp);
static void sort_task_indices(int fd);
static bool save_unmatched_sends = false;
static size_t _ign;

PipDBHeader pipdb_header = {
	magic: { 'P', 'I', 'P' },
	version: PIPDB_VERSION,
	first_ts: {INT_MAX, INT_MAX},
	last_ts: {0, 0},
	// all others zero
};

/* HACK: reused structures!
 * After the first pass, "paths" maps path ID to the lengths (in bytes) of
 * the tasks, notices, and messages for each path.  "tasks" maps task name
 * to the number of tasks.
 * Writing the task and path indices changes the contents of both
 * structures.
 * For the second pass, "paths" contains the current offset for tasks,
 * notices, and messages in each path.  "tasks" contains the current
 * offset in the task index. */
static std::map<std::string, Path> paths;     // maps path id to path info
static std::map<std::string, TaskEnt> tasks;  // maps task name to index entry offset

static MessageMap sends, receives;
static int errors;

int main(int argc, char **argv) {
	int i;
	char c;
	const char *outfn = NULL;

	while ((c = getopt(argc, argv, "o:")) != -1)
		switch (c) {
			case 'o': outfn = optarg; break;
			case 's': save_unmatched_sends = true; break;
			default:  usage(argv[0]);
		}

	if (!outfn || argc-optind < 1)
		usage(argv[0]);

	FILE *op = fopen(outfn, "w");
	pipdb_header.threads_offset = pipdb_header.pack().size();
	fseek(op, pipdb_header.threads_offset, SEEK_SET);

	fprintf(stderr, "Pass 1");
	for (i=optind; i<argc; i++) {
		first_pass(op, argv[i]);
		fputc('.', stderr);
	}
	fputc('\n', stderr);

	pipdb_header.npaths = paths.size();
	pipdb_header.ntasks = tasks.size();
	fprintf(stderr, "%d paths, %d threads, %d tasks, start=%ld.%06ld, end=%ld.%06ld\n",
		pipdb_header.npaths, pipdb_header.nthreads, pipdb_header.ntasks,
		pipdb_header.first_ts.tv_sec, pipdb_header.first_ts.tv_usec,
		pipdb_header.last_ts.tv_sec, pipdb_header.last_ts.tv_usec);

	pipdb_write_task_index(op);
	pipdb_write_path_index(op);

	fprintf(stderr, "Pass 2");
	for (i=optind; i<argc; i++) {
		second_pass(op, argv[i], i-optind+1);
		fputc('.', stderr);
	}
	fputc('\n', stderr);

	check_unpaired_messages(op);

	fseek(op, 0, SEEK_SET);
	std::string header_str = pipdb_header.pack();
	_ign = fwrite(header_str.data(), header_str.size(), 1, op);

	fclose(op);
	int fd = open(outfn, O_RDWR);
	sort_task_indices(fd);
	close(fd);
	printf("There were %d error%s\n", errors, errors==1?"":"s");
	return errors > 0;
}

/* !! this could be made a bit faster.  we don't need to parse all fields
 * of all events, just enough to get the lengths, path names, and task
 * names. */
static void first_pass(FILE *outp, const char *fn) {
	int version = -1;
	FILE *fp = !strcmp(fn, "-") ? stdin : fopen(fn, "r");
	if (!fp) { perror(fn); return; }
	Event *e;
	Path *current_path = NULL;

	while ((e = read_event(version, fp)) != NULL) {
		if (e->tv < pipdb_header.first_ts) pipdb_header.first_ts = e->tv;
		if (e->tv > pipdb_header.last_ts) pipdb_header.last_ts = e->tv;
/* !! we need smarter reconciling logic here.  task and message sizes
 * depend on pairing end+start, recv+send.  so we need to keep big tables
 * of all open tasks and messages.  that's expensive. */
		switch (e->type()) {
			case EV_HEADER:
				if (version != -1) {
					fprintf(stderr, "%s: multiple headers -- did you call ANNOTATE_INIT twice?\n", fn);
					errors++;
				}
				else {
					version = dynamic_cast<Header*>(e)->version;
					pipdb_write_thread(outp, dynamic_cast<Header*>(e));
					pipdb_header.nthreads++;
				}
				break;
			case EV_SET_PATH_ID:
				current_path = &paths[dynamic_cast<NewPathID*>(e)->path_id];
				break;
			case EV_END_TASK:
				tasks[dynamic_cast<EndTask*>(e)->name].tasks++;
				current_path->tasks += pipdb_task_length(NULL, dynamic_cast<EndTask*>(e));
				break;
			case EV_NOTICE:
				current_path->notices += pipdb_notice_length(dynamic_cast<Notice*>(e));
				break;
			case EV_RECV:
				current_path->messages += pipdb_message_length(dynamic_cast<Message*>(e), NULL);
				break;
			case EV_SEND:
			case EV_START_TASK:
			case EV_END_PATH_ID:
			case EV_BELIEF_FIRST:
			case EV_BELIEF:;
		}
		//e->print(stdout, 2);
		delete e;
	}
	if (version == -1) {
		fprintf(stderr, "%s: no header -- zero-length log file?\n", fn);
		errors++;
	}
	fclose(fp);
}

static void usage(const char *prog) {
	fprintf(stderr, "Usage:\n  %s -o outputfile file [file [file [...]]]\n\n", prog);
	exit(1);
}

static void second_pass(FILE *outp, const char *fn, int thread_id) {
	Header *header = NULL;
	Path *current_path = NULL;
	FILE *fp = !strcmp(fn, "-") ? stdin : fopen(fn, "r");
	if (!fp) { perror(fn); return; }
	Event *ev;
	Message *mev;
	StartTask *stev;
	EndTask *etev;
	MessageMap::const_iterator pair_mev;
	while ((ev = read_event(header ? header->version : -1, fp)) != NULL) {
		switch (ev->type()) {
			case EV_HEADER:
				if (header)
					fprintf(stderr, "%s: multiple headers -- did you call ANNOTATE_INIT twice?\n", fn);
				else
					header = dynamic_cast<Header*>(ev);
				break;
			case EV_SET_PATH_ID:
				current_path = &paths[dynamic_cast<NewPathID*>(ev)->path_id];
				delete ev;
				break;
			case EV_START_TASK:
				stev = dynamic_cast<StartTask*>(ev);
				stev->thread_id = thread_id;
				stev->path_id.v = current_path;
				current_path->start_task[stev->name].push_back(stev);
				break;
			case EV_END_TASK:
				etev = dynamic_cast<EndTask*>(ev);
				etev->thread_id = thread_id;
				etev->path_id.v = current_path;
				if (!handle_end_task(outp, etev, current_path)) {
					assert(header);
					current_path->unpaired_tasks[header->hostname].push_back(etev);
					break;
				}
				break;
			case EV_NOTICE:
				pipdb_write_notice(outp, dynamic_cast<Notice*>(ev), thread_id, current_path);
				delete ev;
				break;
			case EV_SEND:
				mev = dynamic_cast<Message*>(ev);
				mev->thread_id = thread_id;
				mev->path_id.v = current_path;
				pair_mev = receives.find(mev->msgid);
				reconcile(outp, mev,
					pair_mev == receives.end() ? NULL : pair_mev->second,
					true, thread_id, current_path);
				break;
			case EV_RECV:
				mev = dynamic_cast<Message*>(ev);
				mev->thread_id = thread_id;
				mev->path_id.v = current_path;
				pair_mev = sends.find(mev->msgid);
				reconcile(outp,
					pair_mev == sends.end() ? NULL : pair_mev->second,
					mev, false, thread_id, current_path);
				break;
			case EV_END_PATH_ID:
			case EV_BELIEF_FIRST:
			case EV_BELIEF:
				delete ev;
				break;
		}
	}
	if (header) delete header;

	check_unpaired_tasks(outp);

	fclose(fp);
}

static void pipdb_write_task_index(FILE *outp) {
	pipdb_header.task_idx_offset = ftell(outp);

	/* each task */
	std::map<std::string, TaskEnt>::iterator tp;
	for (tp=tasks.begin(); tp!=tasks.end(); tp++) {
		tp->second.name_ofs = ftell(outp);  // offset to the name
		_ign = fwrite(tp->first.data(), 1, tp->first.size(), outp);
		fputc('\0', outp);
		_ign = fwrite(&tp->second, sizeof(int), 1, outp);  /* number of task events */
		int temp_ofs = ftell(outp);  // offset to the first slot of this task's index
		fseek(outp, tp->second.tasks*sizeof(int), SEEK_CUR);
		tp->second.tasks = temp_ofs;
	}
}

static void pipdb_write_path_index(FILE *outp) {
	pipdb_header.path_idx_offset = ftell(outp);
	int path_idx_size = 0;

	std::map<std::string, Path>::iterator pp;
	for (pp=paths.begin(); pp!=paths.end(); pp++)
		path_idx_size += sizeof(short) + pp->first.size() + 3*sizeof(int);

	int ofs = pipdb_header.path_idx_offset + path_idx_size;

	/* each path */
	for (pp=paths.begin(); pp!=paths.end(); pp++) {
		short s_temp = pp->first.size();
		_ign = fwrite(&s_temp, sizeof(short), 1, outp);
		_ign = fwrite(pp->first.data(), 1, pp->first.size(), outp);

		int temp = pp->second.tasks;
		_ign = fwrite(&ofs, sizeof(int), 1, outp);
		pp->second.tasks = ofs;
		ofs += temp;

		temp = pp->second.notices;
		_ign = fwrite(&ofs, sizeof(int), 1, outp);
		pp->second.notices = ofs;
		ofs += temp;

		temp = pp->second.messages;
		_ign = fwrite(&ofs, sizeof(int), 1, outp);
		pp->second.messages = ofs;
		ofs += temp;
	}
}

static void pipdb_write_thread(FILE *outp, Header *hdr) {
	fputs(hdr->hostname, outp);
	fputc('\0', outp);
	fputs(hdr->processname, outp);
	fputc('\0', outp);
	_ign = fwrite(&hdr->pid, sizeof(int), 1, outp);
	_ign = fwrite(&hdr->tid, sizeof(int), 1, outp);
	_ign = fwrite(&hdr->ppid, sizeof(int), 1, outp);
	_ign = fwrite(&hdr->uid, sizeof(int), 1, outp);
	int sec = hdr->tv.tv_sec;  _ign = fwrite(&sec, sizeof(sec), 1, outp);
	int usec = hdr->tv.tv_usec;  _ign = fwrite(&usec, sizeof(usec), 1, outp);
	_ign = fwrite(&hdr->tz, sizeof(int), 1, outp);
}

/* be clever with flags fields to save space */
static int pipdb_task_length(StartTask *start, EndTask *end) {
	return 2 + 4 + 2*8 + 9*4;
}

static int pipdb_notice_length(Notice *notice) {
	return strlen(notice->str) + 1 + 8 + 4;
}

static int pipdb_message_length(Message *send, Message *recv) {
	return 1 + 2 + send->msgid.size() + 2*8 + 3*4;
}

// !! use the flags field
static void pipdb_write_task(FILE *outp, Task *start, Task *end, Path *current_path) {
	// seek to where it actually goes, write it
	fseek(outp, current_path->tasks, SEEK_SET);
	struct {
		unsigned short flags;
		int nameidx;
		int start_sec, start_usec, end_sec, end_usec;
		int realtime, utime, stime, minfault, majfault, volcs, involcs, s_thread, e_thread;
	} __attribute__((__packed__)) outbuf = { 0xffff, tasks[end->name].name_ofs,
		start->tv.tv_sec, start->tv.tv_usec,
		end->tv.tv_sec, end->tv.tv_usec,
		end->tv - start->tv,
		end->utime - start->utime,
		end->stime - start->stime,
		end->major_fault - start->major_fault,
		end->minor_fault - start->minor_fault,
		end->vol_cs - start->vol_cs,
		end->invol_cs - start->invol_cs,
		start->thread_id, end->thread_id
	};

	_ign = fwrite(&outbuf, sizeof(outbuf), 1, outp);
	
	// seek to its spot in the index and write the offset
	fseek(outp, tasks[end->name].tasks, SEEK_SET);
	_ign = fwrite(&current_path->tasks, sizeof(int), 1, outp);

	current_path->tasks += sizeof(outbuf);
	tasks[end->name].tasks += sizeof(int);
}

static void pipdb_write_notice(FILE *outp, Notice *notice, int thread_id, Path *current_path) {
	// seek to where it actually goes, write it
	fseek(outp, current_path->notices, SEEK_SET);
	fputs(notice->str, outp);
	fputc('\0', outp);
	int sec = notice->tv.tv_sec;  _ign = fwrite(&sec, sizeof(sec), 1, outp);
	int usec = notice->tv.tv_usec;  _ign = fwrite(&usec, sizeof(usec), 1, outp);
	_ign = fwrite(&thread_id, sizeof(thread_id), 1, outp);
	current_path->notices += strlen(notice->str) + 1 + 8 + 4;
}

// !! use the flags field
static void pipdb_write_message(FILE *outp, Message *send, Message *recv, Path *current_path) {
	// seek to where it actually goes, write it
	fseek(outp, current_path->messages, SEEK_SET);
	fputc(0xff, outp);  // flags
	short idlen = send->msgid.size();
	_ign = fwrite(&idlen, sizeof(idlen), 1, outp);
	_ign = fwrite(send->msgid.data(), send->msgid.size(), 1, outp);
	struct {
		int send_sec, send_usec, recv_sec, recv_usec;
		int size, s_thread, r_thread;
	} __attribute__((__packed__)) outbuf = {
		send->tv.tv_sec, send->tv.tv_usec,
		recv ? recv->tv.tv_sec : send->tv.tv_sec, recv ? recv->tv.tv_usec : send->tv.tv_usec,
		send->size,
		send->thread_id,
		recv ? recv->thread_id : -1
	};

	_ign = fwrite(&outbuf, sizeof(outbuf), 1, outp);
	current_path->messages += 1 + 2 + send->msgid.size() + sizeof(outbuf);
}

bool handle_end_task(FILE *outp, Task *end, Path *path) {
	//!! use find() so it doesn't auto-create entire vectors
	StartList *evl = &path->start_task[end->name];
	if (evl == NULL || evl->empty())
		return false;
	Task *start = (Task*)evl->back();
	evl->pop_back();
	assert(start->path_id.v == end->path_id.v);
	if (evl->empty()) path->start_task.erase(start->name);
	pipdb_write_task(outp, start, end, path);
	delete start;
	delete end;
	return true;
}

static void reconcile(FILE *outp, Message *send, Message *recv, bool is_send, int thread_id, Path *path) {
	assert(thread_id != -1);
	if (is_send)
		send->thread_id = thread_id;
	else
		recv->thread_id = thread_id;
	if (send && recv) {
		if (send->path_id.v != recv->path_id.v) {
			fprintf(stderr, "send/recv path_id mismatch:\n  "); send->print(stderr);
			fprintf(stderr, "  "); recv->print(stderr);
			errors++;
		}
		else if (send->size != recv->size) {
			fprintf(stderr, "packet size mismatch:\n  "); send->print(stderr);
			fprintf(stderr, "  "); recv->print(stderr);
			//abort();
			errors++;
		}
		else
			pipdb_write_message(outp, send, recv, path);

		if (is_send)
			receives.erase(recv->msgid);
		else
			sends.erase(send->msgid);
		delete recv;
		delete send;
	}
	else {
		MessageMap &table = is_send ? sends : receives;
		Message *msg = is_send ? send : recv;
		if (table[msg->msgid] != NULL) {
			fprintf(stderr, "Reused message id:\n  OLD: "); table[msg->msgid]->print(stderr);
			fprintf(stderr, "  NEW: "); msg->print(stderr);
			//abort();
			errors++;
		}
		table[msg->msgid] = msg;
	}
}

// !! check both unpaired_tasks and all paths
// print all tasks starts and ends left in the hash table
static void check_unpaired_tasks(FILE *outp) {
#if 0
	std::map<std::string, std::vector<Task*> >::const_iterator tasksetp;
	for (tasksetp=unpaired_tasks.begin(); tasksetp!=unpaired_tasks.end(); tasksetp++) {
		// host is tasksetp->first
		// set of unmatched tasks (starts and ends together) is tasksetp->second
		PathNameTaskMap start_task;
		for (std::set<Task*, ltEvP>::const_iterator taskp=tasksetp->second.begin(); taskp!=tasksetp->second.end(); taskp++) {
			Task *ev = (Task*)(*taskp);
			assert(ev);
			switch (ev->type()) {
				case EV_START_TASK:{
						StartList *evl = &start_task[ev->path_id][ev->name];
						evl->push_back((StartTask*)ev);
					}
					break;
				case EV_END_TASK:{
						if (!handle_end_task((EndTask*)ev, start_task)) {
							fprintf(stderr, "task end without start on %s, path %d: ", tasksetp->first.c_str(), ev->path_id);
							ev->print(stderr);
							errors++;
						}
					}
					break;
				default:
					fprintf(stderr, "Unexpected event type in unpaired_tasks table: %d\n", ev->type());
					abort();
			}
			//fprintf(stderr, "Unpaired task on %s: ", tasksetp->first.c_str()); ev->print(stderr);
		}

		// now let's see what's left in start_task
		for (PathNameTaskMap::const_iterator pathp=start_task.begin(); pathp!=start_task.end(); pathp++)
			for (NameTaskMap::const_iterator namep=pathp->second.begin(); namep!=pathp->second.end(); namep++)
				for (StartList::const_iterator eventp=namep->second.begin(); eventp!=namep->second.end(); eventp++) {
					fprintf(stderr, "task start without end on %s, path %d:\n", tasksetp->first.c_str(), (*eventp)->path_id);
					(*eventp)->print(stderr, 0);
					errors++;
				}
	}
#endif
}

// print all sends and receives left in the hash table
static void check_unpaired_messages(FILE *outp) {
	MessageMap::const_iterator msgp;

	fprintf(stderr, "Unmatched send count = %zd\n", sends.size());
	if (save_unmatched_sends)
		for (msgp=sends.begin(); msgp!=sends.end(); msgp++)
			pipdb_write_message(outp, msgp->second, NULL, (Path*)msgp->second->path_id.v);
	else
		for (msgp=sends.begin(); msgp!=sends.end(); msgp++) {
			fprintf(stderr, "Unmatched send: ");
			msgp->second->print(stderr);
			errors++;
		}

	fprintf(stderr, "Unmatched recv count = %zd\n", receives.size());
	for (msgp=receives.begin(); msgp!=receives.end(); msgp++) {
		fprintf(stderr, "Unmatched recv: ");
		msgp->second->print(stderr);
		errors++;
	}
}
 
static int cmp_int(const int *a, const int *b) { return *a - *b; }
static void sort_task_indices(int fd) {
	fputs("Sorting task indices", stderr);
	struct stat st;
	fstat(fd, &st);
	char *map = (char*)mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == (char*)-1) { perror("mmap"); exit(1); }

	for (std::map<std::string, TaskEnt>::const_iterator taskp = tasks.begin();
			taskp != tasks.end();
			taskp++) {
		char *name = map + taskp->second.name_ofs;
		int *count = (int*)(name + taskp->first.size() + 1);
		qsort(count+1, *count, sizeof(int), (int(*)(const void*, const void*))cmp_int);
		fputc('.', stderr);
	}

	munmap(map, st.st_size);
	fputc('\n', stderr);
}
