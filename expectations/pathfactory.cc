#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <algorithm>
#include "common.h"
#include "pathfactory.h"
#include "pipdb.h"
#include "rcfile.h"

struct Quant {
	const char *task_query;
	int task_divisor;
};
static Quant quant_map[] = {
	{ "start/1000", 1000 },      // start time
	{ "end-start", 1000 },       // real time
	{ "utime+stime", 1000 },     // total CPU time
	{ "utime", 1000 },           // user CPU time
	{ "stime", 1000 },           // system CPU time
	{ "major_fault", 1 },        // major faults
	{ "minor_fault", 1 },        // minor faults
	{ "vol_cs", 1 },             // voluntary context switches
	{ "invol_cs", 1 },           // involuntary context switches
	{ NULL, 0 },                 // message latency
	{ NULL, 0 },                 // message count
	{ NULL, 0 },                 // total message size
	{ NULL, 0 },                 // tree depth
	{ NULL, 0 },                 // threads
	{ NULL, 0 },                 // hosts
	//!! might be nice to implement latency, messages, and bytes for tasks
};

#ifdef HAVE_MYSQL
static void run_sql(MYSQL *mysql, const char *query);
static void run_sqlf(MYSQL *mysql, const char *fmt, ...);
#endif // HAVE_MYSQL

inline bool operator< (const GraphPoint &a, const GraphPoint &b) {
	return a.x < b.x;
}

static void parse_filter(const std::string &filter, const char **real_filter, bool *negate) {
	if (filter.size() >= 2 && filter[0] == '!') {
		*negate = true;
		*real_filter = filter.c_str() + 1;
	}
	else {
		*negate = false;
		*real_filter = filter.c_str();
	}
}

PathFactory::~PathFactory(void) {}

int PathFactory::find_thread_pool(const StringInt &where) const {
	ThreadPoolMap::const_iterator p = thread_pool_map.find(where);
	if (p == thread_pool_map.end())
		return -1;
	else
		return p->second;
}

#ifdef HAVE_MYSQL
MySQLPathFactory::MySQLPathFactory(const char *_table_base)
		: table_base(strdup(_table_base)) {
	mysql_init(&mysql);
	if (mysql_real_connect(&mysql, config["db.host"], config.get("db.user", getlogin()),
			config["db.password"], config.get("db.name", "pip"), 0, NULL, 0)) {
		get_threads();
		is_valid = true;
	}
	else {
		fprintf(stderr, "Connection failed: %s\n", mysql_error(&mysql));
		is_valid = false;
	}
}

MySQLPathFactory::~MySQLPathFactory(void) {
	free(table_base);
	mysql_close(&mysql);
}

std::vector<int> MySQLPathFactory::get_path_ids(void) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	std::vector<int> pathids;

	// !! we used to select from tasks+notices+messages we don't return empties
	run_sqlf(&mysql, "SELECT * from %s_paths", table_base);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL)
		pathids.push_back(atoi(row[0]));
	mysql_free_result(res);

	return pathids;
}

std::vector<NameRec> MySQLPathFactory::get_path_ids(const std::string &filter) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	std::vector<NameRec> pathids;

	std::string query("SELECT pathid,pathblob FROM ");
	query.append(table_base).append("_paths");
	if (!filter.empty()) {
		if (filter[0] == '!')
			query.append(" WHERE pathblob NOT LIKE '%%").append(filter.substr(1)).append("%%'");
		else
			query.append(" WHERE pathblob LIKE '%%").append(filter).append("%%'");
	}

	// !! we used to select from tasks+notices+messages we don't return empties
	run_sql(&mysql, query.c_str());
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL)
		pathids.push_back(NameRec(row[1], atoi(row[0])));
	mysql_free_result(res);

	return pathids;
}

void MySQLPathFactory::get_threads(void) {
	thread_pool_map.clear();;
	threads.clear();
	int next_thread_pool_id = 1;

	fprintf(stderr, "Reading threads...");
	run_sqlf(&mysql, "SELECT * from %s_threads", table_base);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		PathThread *thr = new PathThread(
			atoi(row[0]),                          // thread id
			// row[1] is roles
			row[1],                                // host
			row[2],                                // prog
			atoi(row[3]),                          // pid
			atoi(row[4]),                          // tid
			atoi(row[5]),                          // ppid
			atoi(row[6]),                          // uid
			ts_to_tv(strtoll(row[7], NULL, 10)),   // start
			atoi(row[8]));                         // tz
		threads[atoi(row[0])] = thr;
		StringInt key(thr->host, thr->pid);
		ThreadPoolMap::iterator p = thread_pool_map.find(key);
		if (p == thread_pool_map.end())
			thread_pool_map[key] = next_thread_pool_id++;

		thr->pool = thread_pool_map[key];
	}
	mysql_free_result(res);
	fprintf(stderr, " done: %zd found.\n", threads.size());
}

void cmp_time(MYSQL *mysql, timeval *min_time, timeval *max_time) {
	MYSQL_RES *res = mysql_use_result(mysql);
	MYSQL_ROW row = mysql_fetch_row(res);
	if (!row || !row[0]) {
		mysql_free_result(res);
		return;
	}
	timeval low = ts_to_tv(strtoll(row[0], NULL, 10));
	timeval high = ts_to_tv(strtoll(row[1], NULL, 10));
	if (min_time->tv_sec == 0 || low < *min_time) *min_time = low;
	if (max_time->tv_sec == 0 || high > *max_time) *max_time = high;
	while (mysql_fetch_row(res)) ;
	mysql_free_result(res);
}

std::pair<timeval, timeval> MySQLPathFactory::get_times(void) {
	// cache the result
	static std::pair<timeval, timeval> times(make_tv(0,0), make_tv(0,0));
	if (times.second.tv_sec != 0) return times;

	run_sqlf(&mysql, "SELECT MIN(start),MAX(start) FROM %s_tasks", table_base);
	cmp_time(&mysql, &times.first, &times.second);
	run_sqlf(&mysql, "SELECT MIN(ts),MAX(ts) FROM %s_notices", table_base);
	cmp_time(&mysql, &times.first, &times.second);
	run_sqlf(&mysql, "SELECT MIN(ts_send),MAX(ts_send) FROM %s_messages", table_base);
	cmp_time(&mysql, &times.first, &times.second);
	run_sqlf(&mysql, "SELECT MIN(ts_recv),MAX(ts_recv) FROM %s_messages", table_base);
	cmp_time(&mysql, &times.first, &times.second);

	return times;
}

std::vector<NameRec> MySQLPathFactory::get_tasks(const std::string &filter) {
	std::vector<NameRec> tasks;

	std::string query("SELECT name,COUNT(name) FROM ");
	query.append(table_base).append("_tasks");
	if (!filter.empty()) {
		if (filter.size() >= 2 && filter[0] == '!')
			query.append(" WHERE name NOT LIKE '%%").append(filter.substr(1)).append("%%'");
		else
			query.append(" WHERE name LIKE '%%").append(filter).append("%%'");
	}
	query.append(" GROUP BY name LIMIT 1000");
	run_sql(&mysql, query.c_str());
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL)
		tasks.push_back(NameRec(row[0], atoi(row[1])));
	mysql_free_result(res);

	return tasks;
}

std::vector<ThreadPoolRec> MySQLPathFactory::get_thread_pools(const std::string &filter) {
	std::vector<ThreadPoolRec> pools;
	std::string query("SELECT host,pid,COUNT(host) FROM ");
	query.append(table_base).append("_threads");
	if (!filter.empty()) {
		if (filter.size() >= 2 && filter[0] == '!')
			query.append(" WHERE host NOT LIKE '%%").append(filter.substr(1)).append("%%'");
		else
			query.append(" WHERE host LIKE '%%").append(filter).append("%%'");
	}
	query.append(" GROUP BY host,pid LIMIT 1000");
	run_sql(&mysql, query.c_str());
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL)
		pools.push_back(ThreadPoolRec(row[0], atoi(row[1]), atoi(row[2])));
	mysql_free_result(res);

	return pools;
}

std::vector<GraphPoint> MySQLPathFactory::get_task_metric(const std::string &name,
			GraphQuantity quant, GraphStyle style, int max_points) {
	std::vector<GraphPoint> data;

	if (!quant_map[quant].task_query) {
		fprintf(stderr, "quant %d is not defined for tasks (yet?)\n", quant);
		return data;
	}

	int row_count=0, skip=0;
	MYSQL_RES *res;
	MYSQL_ROW row;
	if (style == STYLE_CDF || style == STYLE_TIME) {
		run_sqlf(&mysql, "SELECT COUNT(*) FROM %s_tasks WHERE name='%s'", table_base, name.c_str());
		res = mysql_use_result(&mysql);
		row = mysql_fetch_row(res);
		row_count = atoi(row[0]);
		assert(row_count > 0);
		skip = row_count / (max_points - 1) + 1;
		mysql_free_result(res);
		if (style == STYLE_CDF && row_count == 1) return data;  // don't plot invalid CDF
	}

	std::pair<timeval, timeval> times = get_times();
	char subtract_start[32] = "";
	if (quant == QUANT_START)
		sprintf(subtract_start, "-%ld%03ld", times.first.tv_sec, times.first.tv_usec/1000);
	switch (style) {
		case STYLE_CDF:
			run_sqlf(&mysql, "SELECT %s%s AS x,pathid FROM %s_tasks WHERE name='%s' ORDER BY x",
				quant_map[quant].task_query, subtract_start,
				table_base, name.c_str());
			break;
		case STYLE_PDF:
			run_sqlf(&mysql, "SELECT ROUND((%s%s)/%d) AS x,COUNT(name),pathid FROM %s_tasks WHERE name='%s' GROUP BY x",
				quant_map[quant].task_query, subtract_start,
				quant_map[quant].task_divisor, table_base, name.c_str());
			break;
		case STYLE_TIME:
			run_sqlf(&mysql, "SELECT %s%s AS x,start/1000000-%ld,pathid FROM %s_tasks WHERE name='%s' ORDER BY start",
				quant_map[quant].task_query, subtract_start,
				times.first.tv_sec, table_base, name.c_str());
			break;
	}
	res = mysql_use_result(&mysql);
	int n = 0, x, last_x = 1<<30;
	int divisor = quant_map[quant].task_divisor;
	while ((row = mysql_fetch_row(res)) != NULL) {
		switch (style) {
			case STYLE_CDF:
				if (n % skip == 0 || n == row_count - 1) {
					data.push_back(GraphPoint(atof(row[0])/divisor, (double)n/(row_count-1), atoi(row[1])));
					//printf("%f,%f => %d\n", atof(row[0])/divisor, (double)n/(row_count-1), atoi(row[1]));
				}
				n++;
				break;
			case STYLE_PDF:
				x = atoi(row[0]);
				if (x == last_x + 2)
					data.push_back(GraphPoint(x-1, 0, -1));
				else if (x > last_x + 2) {
					data.push_back(GraphPoint(last_x+1, 0, -1));
					data.push_back(GraphPoint(x-1, 0, -1));
				}
				last_x = x;
				n = atoi(row[1]);
				data.push_back(GraphPoint(x, n, atoi(row[2])));
				break;
			case STYLE_TIME:
				if (n % skip == 0 || n == row_count - 1)
					data.push_back(GraphPoint(atof(row[1]), atof(row[0])/divisor, atoi(row[2])));
				n++;
				break;
		}
	}
	mysql_free_result(res);

	return data;
}

Path *MySQLPathFactory::get_path(int pathid) {
	Path *ret = new Path();
	ret->path_id = pathid;

	run_sqlf(&mysql, "SELECT * FROM %s_tasks WHERE pathid=%d ORDER BY start",
		table_base, pathid);
	MYSQL_RES *res = mysql_use_result(&mysql);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathTask *pt = new PathTask(
			atoi(row[0]),                          // path id
			// row[1] is roles
			atoi(row[2]),                          // level
			strdup(row[3]),                        // name
			ts_to_tv(strtoll(row[4], NULL, 10)),   // ts
			ts_to_tv(strtoll(row[5], NULL, 10)),   // ts_end
			atoi(row[6]),                          // tdiff
			atoi(row[7]),                          // utime
			atoi(row[8]),                          // stime
			atoi(row[9]),                          // major_fault
			atoi(row[10]),                         // minor_fault
			atoi(row[11]),                         // vol_cs
			atoi(row[12]),                         // invol_cs
			atoi(row[13]));                        // thread_id
		ret->insert(pt);
	}
	mysql_free_result(res);

	run_sqlf(&mysql, "SELECT * FROM %s_notices WHERE pathid=%d", table_base, pathid);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathNotice *pn = new PathNotice(
			atoi(row[0]),                          // path id
			// row[1] is roles
			atoi(row[2]),                          // level
			strdup(row[3]),                        // name
			ts_to_tv(strtoll(row[4], NULL, 10)),   // ts
			atoi(row[5]));                         // thread_id
		//pn->print(stderr);
		ret->insert(pn);
	}
	mysql_free_result(res);

	run_sqlf(&mysql, "SELECT * FROM %s_messages WHERE pathid=%d", table_base, pathid);
	res = mysql_use_result(&mysql);
	while ((row = mysql_fetch_row(res)) != NULL) {
		assert(atoi(row[0]) == pathid);
		PathMessage *pm = new PathMessage(
			atoi(row[0]),                          // path id
			// row[1] is roles
			atoi(row[2]),                          // level
			// row[3] is msgid
			ts_to_tv(strtoll(row[4], NULL, 10)),   // ts_send
			ts_to_tv(strtoll(row[5], NULL, 10)),   // ts_recv
			atoi(row[6]),                          // size
			atoi(row[7]),                          // thread_send
			atoi(row[8]));                         // thread_recv
		//pm->print(stderr);
		ret->insert(pm);
	}
	mysql_free_result(res);

	ret->done_inserting();
	return ret;
}

std::string MySQLPathFactory::get_name(void) const {
	return std::string("MYSQL:")+config.get("db.name", "pip")+"."+table_base;
}

static void run_sql(MYSQL *mysql, const char *query) {
	//fprintf(stderr, "SQL(\"%s\")\n", query);
	if (mysql_query(mysql, query) != 0) {
		fprintf(stderr, "Database error:\n");
		fprintf(stderr, "  QUERY: \"%s\"\n", query);
		fprintf(stderr, "  MySQL error: \"%s\"\n", mysql_error(mysql));
		exit(1);
	}
}

static void run_sqlf(MYSQL *mysql, const char *fmt, ...) {
	char query[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(query, fmt, arg);
	va_end(arg);
	run_sql(mysql, query);
}
#endif // HAVE_MYSQL


/**************************************************************************/

PipDBPathFactory::PipDBPathFactory(const char *_filename)
		: filename(strdup(_filename)), map(NULL) {
	int fd = open(_filename, O_RDONLY);
	is_valid = false;
	if (fd == -1) {
		perror(_filename);
		return;
	}
	struct stat st;
	fstat(fd, &st);
	maplen = st.st_size;
	map = (char*)mmap(NULL, maplen, PROT_READ, MAP_SHARED, fd, 0);
	if (map == (char*)-1) {
		perror("mmap");
		map = NULL;
		return;
	}
	close(fd);   // region remains mapped

	pipdb_header = PipDBHeader::unpack(map);
	if (strncmp(pipdb_header.magic, "PIP", 3) != 0) {
		fprintf(stderr, "%s: invalid header\n", _filename);
		return;
	}

	//fprintf(stderr, "pipdb: version %d\n", pipdb_header.version);

	char *readp = map + pipdb_header.task_idx_offset;
	for (int i=0; i<pipdb_header.ntasks; i++) {
		int len = strlen(readp);
		//fprintf(stderr, "task: \"%s\"\n", readp);
		task_idx[readp] = readp - map + len + 1;
		readp += len + 1;
		readp += (*(int*)readp + 1) * sizeof(int);
	}

	readp = map + pipdb_header.path_idx_offset;
	for (int i=0; i<pipdb_header.npaths; i++) {
		short namelen = *(short*)readp;
		//fprintf(stderr, "path: %08x\n", *(int*)(readp+2));
		path_idx.push_back(PipDBPathIndexEnt(readp+2, namelen,
			*(int*)(readp+2+namelen),
			*(int*)(readp+2+namelen+sizeof(int)),
			*(int*)(readp+2+namelen+2*sizeof(int))));
		readp += 2 + namelen + 3*sizeof(int);
	}

	get_threads();
	is_valid = true;
}

PipDBPathFactory::~PipDBPathFactory(void) {
	if (map != NULL) munmap(map, maplen);
	free(filename);
}

static const int printable[96] = {	/* characters 32-127 */
	/* don't print " & ' < > \ #127 */
	1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
};

static const char *ID_to_string(const std::string &id) {
	static char buf[1024];
	char *p = buf;
	bool inbin = false;
	const char *data = id.data();
	int len = id.length();
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

std::vector<int> PipDBPathFactory::get_path_ids(void) {
	std::vector<int> pathids;

	// populate the set with {1..npaths}
	for (int i=1; i<=pipdb_header.npaths; i++)
		pathids.push_back(i);

	return pathids;
}

std::vector<NameRec> PipDBPathFactory::get_path_ids(const std::string &filter) {
	std::vector<NameRec> pathids;
	bool negate;
	const char *real_filter;
	parse_filter(filter, &real_filter, &negate);

	for (int i=1; i<=pipdb_header.npaths; i++) {
		std::string txt_name = ID_to_string(std::string(path_idx[i-1].name, path_idx[i-1].namelen));
		if (!filter.empty()) {
			if (negate) {
				if (txt_name.find(real_filter) != std::string::npos) continue;
			}
			else {
				if (txt_name.find(real_filter) == std::string::npos) continue;
			}
		}
		pathids.push_back(NameRec(txt_name, i));
	}

	return pathids;
}

void PipDBPathFactory::get_threads(void) {
	thread_pool_map.clear();;
	threads.clear();
	int next_thread_pool_id = 1;

	fprintf(stderr, "Reading threads...");

	char *readp = map + pipdb_header.threads_offset;
	for (int thread_id=1; thread_id<=pipdb_header.nthreads; thread_id++) {
		char *host = readp;
		readp += strlen(readp) + 1;
		char *prog = readp;
		readp += strlen(readp) + 1;
		PathThread *thr = new PathThread(
			thread_id,                                    // thread id
			host,                                       
			prog,                                       
			((int*)readp)[0],                             // pid
			((int*)readp)[1],                             // tid
			((int*)readp)[2],                             // ppid
			((int*)readp)[3],                             // uid
			make_tv(((int*)readp)[4], ((int*)readp)[5]),  // ts
			((int*)readp)[6]);                            // tz
		readp += 7 * sizeof(int);
		threads[thread_id] = thr;
		StringInt key(thr->host, thr->pid);
		ThreadPoolMap::iterator p = thread_pool_map.find(key);
		if (p == thread_pool_map.end())
			thread_pool_map[key] = next_thread_pool_id++;

		thr->pool = thread_pool_map[key];
	}
	fprintf(stderr, " done: %zd found.\n", threads.size());
}

std::pair<timeval, timeval> PipDBPathFactory::get_times(void) {
	return std::pair<timeval, timeval>(pipdb_header.first_ts, pipdb_header.last_ts);
}

std::vector<NameRec> PipDBPathFactory::get_tasks(const std::string &filter) {
	std::vector<NameRec> tasks;
	bool negate;
	const char *real_filter;
	parse_filter(filter, &real_filter, &negate);

	for (std::map<const char *, int, ltstr>::const_iterator idxp = task_idx.begin();
			idxp != task_idx.end();
			idxp++) {
		if (!filter.empty()) {
			if (negate) {
				if (strstr(idxp->first, real_filter) != NULL) continue;
			}
			else {
				if (strstr(idxp->first, real_filter) == NULL) continue;
			}
		}
		tasks.push_back(NameRec(idxp->first, *(int*)(map+idxp->second)));
	}

	return tasks;
}

std::vector<ThreadPoolRec> PipDBPathFactory::get_thread_pools(const std::string &filter) {
	std::vector<ThreadPoolRec> pools;
	bool negate;
	const char *real_filter;
	parse_filter(filter, &real_filter, &negate);

	std::map<StringInt, int> pools_count;
	for (ThreadPoolMap::const_iterator tpp = thread_pool_map.begin();
			tpp != thread_pool_map.end();
			tpp++)
		pools_count[tpp->first]++;

	for (std::map<StringInt, int>::const_iterator countp = pools_count.begin();
			countp != pools_count.end();
			countp++)
		pools.push_back(ThreadPoolRec(countp->first.first, countp->first.second, countp->second));

	return pools;
}

static float get_val(GraphQuantity quant, const char *taskp, const timeval &first_ts) {
	switch (quant) {
		case QUANT_START:
			return (*(timeval*)(taskp + 6) - first_ts) / 1000000.0;   // sec
		case QUANT_REAL:
			return ((*(timeval*)(taskp + 14) - first_ts)
				- (*(timeval*)(taskp + 6) - first_ts)) / 1000.0;  // ms
		case QUANT_CPU:
			return (*(int*)(taskp + 26))/1000.0
				+ (*(int*)(taskp + 30))/1000.0;   // ms
		case QUANT_UTIME:
			return (*(int*)(taskp + 26))/1000.0; // ms
		case QUANT_STIME:
			return (*(int*)(taskp + 30))/1000.0; // ms
		case QUANT_MAJFLT:
			return *(int*)(taskp + 34);
		case QUANT_MINFLT:
			return *(int*)(taskp + 38);
		case QUANT_VCS:
			return *(int*)(taskp + 42);
		case QUANT_IVCS:
			return *(int*)(taskp + 46);
		default: assert(!"invalid quant");
	}
}

bool operator< (const std::pair<float, int> &a, const std::pair<float, int> &b) {
	return a.first < b.first;
}
std::vector<GraphPoint> PipDBPathFactory::get_task_metric(const std::string &name,
			GraphQuantity quant, GraphStyle style, int max_points) {
	std::vector<GraphPoint> data;

	if (!quant_map[quant].task_query) {
		fprintf(stderr, "quant %d is not defined for tasks (yet?)\n", quant);
		return data;
	}

	int *idxp = (int*)(map + task_idx[name.c_str()]);
	int row_count = *(idxp++);
	assert(row_count > 0);
	switch (style) {
		case STYLE_CDF:{
			if (row_count == 1) return data;  // don't plot invalid CDF

			std::vector<std::pair<float, int> > temp_data;
			for (int i=0; i<row_count; i++)
				temp_data.push_back(std::pair<float, int>(
					get_val(quant, map + idxp[i], pipdb_header.first_ts), get_pathid_by_ofs(idxp[i])));
			sort(temp_data.begin(), temp_data.end());

			int skip = row_count / (max_points - 1) + 1;
			for (int i=0; i<row_count; i+=skip)
				data.push_back(GraphPoint(temp_data[i].first, (double)i/(row_count-1), temp_data[i].second));
			if ((row_count - 1) % skip != 0)
				data.push_back(GraphPoint(temp_data[row_count-1].first, 1, temp_data[row_count-1].second));

			}break;
		case STYLE_PDF:{
			std::map<int, std::pair<int, int> > temp_data;  // val -> { count, pathid }
			for (int i=0; i<row_count; i++) {
				int val = (int)round(get_val(quant, map + idxp[i], pipdb_header.first_ts));
				if (temp_data[val].first++ == 1)
					temp_data[val].second = get_pathid_by_ofs(idxp[i]);
			}

			int x, last_x = 1<<30;
			for (std::map<int, std::pair<int, int> >::const_iterator datap = temp_data.begin();
					datap != temp_data.end();
					datap++) {
				x = (int)round(datap->first);
				if (x == last_x + 2)
					data.push_back(GraphPoint(x-1, 0, -1));
				else if (x > last_x + 2) {
					data.push_back(GraphPoint(last_x+1, 0, -1));
					data.push_back(GraphPoint(x-1, 0, -1));
				}
				last_x = x;
				data.push_back(GraphPoint(x, datap->second.first, datap->second.second));
			}
			}break;
		case STYLE_TIME:
			int skip = row_count / (max_points - 1) + 1;
			for (int i=0; i<row_count; i+=skip) {
				float val = get_val(quant, map + idxp[i], pipdb_header.first_ts);
				int tdiff = *(timeval*)(map + idxp[i] + 6) - pipdb_header.first_ts;
				data.push_back(GraphPoint(tdiff/1000000.0, val, get_pathid_by_ofs(idxp[i])));
			}
			sort(data.begin(), data.end());
	}

	return data;
}

struct _ltpt {
	bool operator()(const PathTask *a, const PathTask *b) const { return a->ts < b->ts; }
} ltpt;
Path *PipDBPathFactory::get_path(int pathid) {
	Path *ret = new Path();
	ret->path_id = pathid;

	char *taskofs = map + path_idx[pathid-1].taskofs;
	char *noticeofs = map + path_idx[pathid-1].noticeofs;
	char *messageofs = map + path_idx[pathid-1].messageofs;
	char *end = map + ((pathid == (int)path_idx.size()) ? maplen : path_idx[pathid].taskofs);

	char *readp = taskofs;
	std::vector<PathTask*> tasks;
	while (readp < noticeofs) {
		unsigned short flags = *(unsigned short*)readp;
		if (!flags) break;   // empty space
		readp += sizeof(unsigned short);
		int *arr = (int*)readp;
		readp += 14 * sizeof(int);
		PathTask *pt = new PathTask(
			pathid,
			0,                                     // level
			map+arr[0],                            // name
			make_tv(arr[1], arr[2]),               // ts
			make_tv(arr[3], arr[4]),               // ts_end
			arr[5],                                // tdiff
			arr[6],                                // utime
			arr[7],                                // stime
			arr[8],                                // major_fault
			arr[9],                                // minor_fault
			arr[10],                               // vol_cs
			arr[11],                               // invol_cs
			arr[12]);                              // thread_id
		//pt->print(stderr);
		tasks.push_back(pt);
	}
	sort(tasks.begin(), tasks.end(), ltpt);
	for (unsigned int i=0; i<tasks.size(); i++)
		ret->insert(tasks[i]);
	tasks.clear();

	readp = noticeofs;
	while (readp < messageofs) {
		char *name = readp;
		if (!name[0]) break;  // empty space
		readp += strlen(name) + 1;
		int *arr = (int*)readp;
		readp += 3 * sizeof(int);
		PathNotice *pn = new PathNotice(
			pathid,
			0,                                     // level
			strdup(name),                          // name
			make_tv(arr[0], arr[1]),               // ts
			arr[2]);                               // thread_id
		ret->insert(pn);
	}

	readp = messageofs;
	while (readp < end) {
		char flags = *readp;
		if (!flags) break;   // empty space
		readp += sizeof(char);
		unsigned short idlen = *(unsigned short*)readp;
		readp += sizeof(unsigned short) + idlen;
		int *arr = (int*)readp;
		readp += 7 * sizeof(int);
		PathMessage *pm = new PathMessage(
			pathid,
			0,                                     // level
			make_tv(arr[0], arr[1]),               // ts_send
			make_tv(arr[2], arr[3]),               // ts_recv
			arr[4],                                // size
			arr[5],                                // thread_send
			arr[6]);                               // thread_recv
		//pm->send->print(stderr);
		//if (pm->recv) pm->recv->print(stderr); else fprintf(stderr, "recv=NULL\n");
		ret->insert(pm);
	}

	ret->done_inserting();
	return ret;
}

std::string PipDBPathFactory::get_name(void) const {
	return std::string("pipdb:")+filename;
}

bool operator<(int a, const PipDBPathIndexEnt &b) { return a < b.taskofs; }
int PipDBPathFactory::get_pathid_by_ofs(int ofs) const {
	// upper_bound returns the first index strictly greater than the test
	// value.  Thus, we want the predecessor, but we also add one because
	// pathids are 1-based, not 0-based.  +1 and -1 cancel out.
	return upper_bound(path_idx.begin(), path_idx.end(), ofs) - path_idx.begin();
}

// it's a factory factory
PathFactory *path_factory(const char *name) {
#ifdef HAVE_MYSQL
	if (!strncasecmp(name, "mysql:", 6)) return new MySQLPathFactory(name+6);
	if (!strncasecmp(name, "pipdb:", 6)) return new PipDBPathFactory(name+6);
	struct stat st;
	if (stat(name, &st) != -1) return new PipDBPathFactory(name);
	return new MySQLPathFactory(name);
#else
	return new PipDBPathFactory(name);
#endif
}
