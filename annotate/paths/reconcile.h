#ifndef RECONCILE_H
#define RECONCILE_H

#include <time.h>
#include <map>
#include <set>
#include <string>
#include <mysql/mysql.h>

struct ltEvP {
  bool operator()(const Event* s1, const Event* s2) const {
    return *s1 < *s2;
  }
};
struct ltstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1, s2) < 0;
  }
};

typedef std::vector<StartTask *> StartList;
typedef std::map<const char *, StartList, ltstr> NameTaskMap;
typedef std::map<int, NameTaskMap> PathNameTaskMap;
typedef std::map<IDBlock, Message*> MessageMap;

extern int errors;
extern MYSQL mysql;
extern std::string table_tasks;
extern std::string table_notices;
extern std::string table_messages;
extern std::string table_threads;
extern std::string table_paths;
extern std::map<std::string, std::set<Task*, ltEvP> > unpaired_tasks;
extern MessageMap sends;
extern MessageMap receives;

long long tv_to_ts(const timeval tv);
void reconcile_init(const char *table_base);
void reconcile_done(void);
void run_sql(const char *fmt, ...) __attribute__((__format__(printf,1,2)));
bool handle_end_task(Task *end, PathNameTaskMap &start_task);

#endif
