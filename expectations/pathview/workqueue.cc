#include <assert.h>
#include <deque>
#include <mysql/mysql.h>
#include <gtk/gtk.h>
#include "path.h"
#include "workqueue.h"

#define ROWS_PER_CALL 50

struct IdleQuery {
	IdleQuery(const std::string &_cmd, RowHandler _row_handler, Notify _start,
			Notify _end, void *_user_data) : cmd(_cmd), row_handler(_row_handler),
			start(_start), end(_end), user_data(_user_data) {}
	std::string cmd;
	RowHandler row_handler;
	Notify start, end;
	void *user_data;
};

static std::deque<IdleQuery> queries;
static MYSQL_RES *res = NULL;
extern MYSQL mysql;
static bool registered = false;

static gboolean the_idle_func(void *ign);
static void next_query(void);

void add_db_idler(const char *query, RowHandler rh, Notify start, Notify end,
		void *user_data) {
	add_db_idler(std::string(query), rh, start, end, user_data);
}

void add_db_idler(const std::string &query, RowHandler rh, Notify start, Notify end,
		void *user_data) {
	queries.push_back(IdleQuery(query, rh, start, end, user_data));
	//printf("WQ: pushing query: \"%s\"\n", query.c_str());
	if (!registered) {
		g_idle_add(the_idle_func, NULL);
		res = NULL;
		registered = TRUE;
	}
}

static void next_query(void) {
	assert(queries.size() != 0);
	IdleQuery *iq = &(*queries.begin());
	if (iq->start) iq->start(iq->user_data);
	//printf("WQ: running query: \"%s\"\n", iq->cmd.c_str());
	run_sql(&mysql, iq->cmd.c_str());
	res = mysql_use_result(&mysql);
}

static gboolean the_idle_func(void *ign) {
	assert(queries.size() != 0);
	if (!res) next_query();
	IdleQuery *iq = &(*queries.begin());
	MYSQL_ROW row;
	int rows_left = ROWS_PER_CALL;
	while (rows_left--) {
		if ((row = mysql_fetch_row(res)) == NULL) {
			mysql_free_result(res);
			if (iq->end) iq->end(iq->user_data);
			queries.pop_front();
			//printf("WQ: query finished, %d remain\n", queries.size());
			if (queries.empty()) {
				registered = FALSE;
				return FALSE;
			}
			else {
				next_query();
				return TRUE;
			}
		}
		else if (iq->row_handler)
			iq->row_handler(row, iq->user_data);
	}
	return TRUE;
}
