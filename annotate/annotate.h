#ifndef ANNOTATE_H
#define ANNOTATE_H

void ANNOTATE_INIT(void);
void ANNOTATE_START_TASK(const char *name);
void ANNOTATE_END_TASK(const char *name);
void ANNOTATE_SET_PATH_ID(const void *path_id, int idsz);
const void *ANNOTATE_GET_PATH_ID(int *len);
void ANNOTATE_END_PATH_ID(const void *path_id, int idsz);
void ANNOTATE_NOTICE(const char *fmt, ...);
void ANNOTATE_SEND(const void *msgid, int idsz, int size);
void ANNOTATE_RECEIVE(const void *msgid, int idsz, int size);

#define ANNOTATE_SET_PATH_ID_INT(n) do{int x=n;ANNOTATE_SET_PATH_ID(&(x), sizeof(x));}while(0)
#define ANNOTATE_END_PATH_ID_INT(n) do{int x=n;ANNOTATE_END_PATH_ID(&(x), sizeof(x));}while(0)
#define ANNOTATE_SEND_INT(n, size) do{int x=n;ANNOTATE_SEND(&(x), sizeof(x), size);}while(0)
#define ANNOTATE_RECEIVE_INT(n, size) do{int x=n;ANNOTATE_RECEIVE(&(x), sizeof(x), size);}while(0)

#if 0
void REAL_ANNOTATE_BELIEF(int condition, float max_fail_rate, const char *condstr, const char *file, int line);
#define ANNOTATE_BELIEF(cond, rate) REAL_ANNOTATE_BELIEF(cond, rate, #cond, __FILE__, __LINE__);
#endif

#endif
