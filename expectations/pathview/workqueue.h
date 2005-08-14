#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <mysql/mysql.h>

typedef void (*RowHandler)(MYSQL_ROW row, void *user_data);
typedef void (*Notify)(void *user_data);
extern MYSQL idle_mysql;

void add_db_idler(const char *query, RowHandler rh, Notify start, Notify end,
		void *user_data);
void add_db_idler(const std::string &query, RowHandler rh, Notify start,
		Notify end, void *user_data);

#endif
