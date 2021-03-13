#ifndef PATH_FACTORY_H
#define PATH_FACTORY_H

#include <map>
#include <vector>
#include <string>
#include "path.h"
#include "pipdb.h"
#ifdef HAVE_MYSQL
#include <mysql/mysql.h>
#endif

typedef std::pair<std::string, int> StringInt;
typedef std::map<StringInt, int> ThreadPoolMap;

struct ThreadPoolRec {
	std::string host;
	int pid, count;
	ThreadPoolRec(const std::string &_host, int _pid, int _count)
			: host(_host), pid(_pid), count(_count) {}
};

struct NameRec {
	std::string name;
	int count;
	NameRec(const std::string &_name, int _count) : name(_name), count(_count) {}
};

struct GraphPoint {
	float x, y;
	int pathid;
	GraphPoint(float _x, float _y, int _pathid) : x(_x), y(_y), pathid(_pathid) {}
};

enum GraphQuantity { QUANT_START=0, QUANT_REAL, QUANT_CPU, QUANT_UTIME,
	QUANT_STIME, QUANT_MAJFLT, QUANT_MINFLT, QUANT_VCS, QUANT_IVCS,
	QUANT_LATENCY, QUANT_MESSAGES, QUANT_BYTES, QUANT_DEPTH, QUANT_THREADS, QUANT_HOSTS };
enum GraphStyle { STYLE_CDF, STYLE_PDF, STYLE_TIME };

class PathFactory {
public:
	virtual ~PathFactory(void);
	virtual std::vector<int> get_path_ids(void) = 0;
	virtual std::vector<NameRec> get_path_ids(const std::string &filter) = 0;
	virtual std::pair<timeval, timeval> get_times(void) = 0;
	virtual std::vector<NameRec> get_tasks(const std::string &filter) = 0;
	virtual std::vector<ThreadPoolRec> get_thread_pools(const std::string &filter) = 0;
	virtual int find_thread_pool(const StringInt &where) const;
	virtual std::vector<GraphPoint> get_task_metric(const std::string &name,
			GraphQuantity quant, GraphStyle style, int max_points) = 0;
	virtual Path *get_path(int pathid) = 0;
	virtual std::string get_name(void) const = 0;
	virtual bool valid(void) { return is_valid; }

protected:
	bool is_valid;
	ThreadPoolMap thread_pool_map;  // map <host,pid> -> thread_pool_id

	// set the global "threads" to a list of all threads; called from the constructor
	virtual void get_threads(void) = 0;
};

#ifdef HAVE_MYSQL
class MySQLPathFactory : public PathFactory {
public:
	MySQLPathFactory(const char *_table_base);
	~MySQLPathFactory(void);
	virtual std::vector<int> get_path_ids(void);
	virtual std::vector<NameRec> get_path_ids(const std::string &filter);
	virtual std::pair<timeval, timeval> get_times(void);
	virtual std::vector<NameRec> get_tasks(const std::string &filter);
	virtual std::vector<ThreadPoolRec> get_thread_pools(const std::string &filter);
	virtual std::vector<GraphPoint> get_task_metric(const std::string &name,
			GraphQuantity quant, GraphStyle style, int max_points);
	virtual Path *get_path(int pathid);
	virtual std::string get_name(void) const;

protected:
	MYSQL mysql;
	char *table_base;

	virtual void get_threads(void);
};
#endif   // HAVE_MYSQL

struct PipDBPathIndexEnt {
	char *name;
	short namelen;
	int taskofs, noticeofs, messageofs;
	PipDBPathIndexEnt(char *_name, short _namelen, int _taskofs, int _noticeofs, int _messageofs)
			: name(_name), namelen(_namelen), taskofs(_taskofs),
			noticeofs(_noticeofs), messageofs(_messageofs) {}
};

struct ltstr {
	bool operator()(const char* s1, const char* s2) const { return strcmp(s1, s2) < 0; }
};

class PipDBPathFactory : public PathFactory {
public:
	PipDBPathFactory(const char *_filename);
	~PipDBPathFactory(void);
	virtual std::vector<int> get_path_ids(void);
	virtual std::vector<NameRec> get_path_ids(const std::string &filter);
	virtual std::pair<timeval, timeval> get_times(void);
	virtual std::vector<NameRec> get_tasks(const std::string &filter);
	virtual std::vector<ThreadPoolRec> get_thread_pools(const std::string &filter);
	virtual std::vector<GraphPoint> get_task_metric(const std::string &name,
			GraphQuantity quant, GraphStyle style, int max_points);
	virtual Path *get_path(int pathid);
	virtual std::string get_name(void) const;

protected:
	char *filename;
	char *map;
	off_t maplen;
	PipDBHeader pipdb_header;
	std::map<const char *, int, ltstr> task_idx;
	std::vector<PipDBPathIndexEnt> path_idx;

	virtual void get_threads(void);
	virtual int get_pathid_by_ofs(int ofs) const;
};

PathFactory *path_factory(const char *name);

#endif
