#ifndef ANNOTATE_H
#define ANNOTATE_H

#ifdef __cplusplus
extern "C" {
#endif

void ANNOTATE_INIT(void);
void ANNOTATE_START_TASK(const char *roles, int level, const char *name);
void ANNOTATE_END_TASK(const char *roles, int level, const char *name);
void ANNOTATE_SET_PATH_ID(const char *roles, int level, const void *path_id, int idsz);
const void *ANNOTATE_GET_PATH_ID(int *len);
void ANNOTATE_END_PATH_ID(const char *roles, int level, const void *path_id, int idsz);
void ANNOTATE_NOTICE(const char *roles, int level, const char *fmt, ...);
void ANNOTATE_SEND(const char *roles, int level, const void *msgid, int idsz, int size);
void ANNOTATE_RECEIVE(const char *roles, int level, const void *msgid, int idsz, int size);

#define ANNOTATE_SET_PATH_ID_INT(roles, level, n) do{int x=n;ANNOTATE_SET_PATH_ID(roles, level, &(x), sizeof(x));}while(0)
#define ANNOTATE_END_PATH_ID_INT(roles, level, n) do{int x=n;ANNOTATE_END_PATH_ID(roles, level, &(x), sizeof(x));}while(0)
#define ANNOTATE_SEND_INT(roles, level, n, size) do{int x=n;ANNOTATE_SEND(roles, level, &(x), sizeof(x), size);}while(0)
#define ANNOTATE_RECEIVE_INT(roles, level, n, size) do{int x=n;ANNOTATE_RECEIVE(roles, level, &(x), sizeof(x), size);}while(0)
void ANNOTATE_SET_PATH_ID_STR(const char *roles, int level, const char *fmt, ...);
void ANNOTATE_END_PATH_ID_STR(const char *roles, int level, const char *fmt, ...);
void ANNOTATE_SEND_STR(const char *roles, int level, int size, const char *fmt, ...);
void ANNOTATE_RECEIVE_STR(const char *roles, int level, int size, const char *fmt, ...);

void ANNOTATE_BELIEF_FIRST(int seq, float max_fail_rate, const char *condstr, const char *file, int line);
void REAL_ANNOTATE_BELIEF(const char *roles, int level, int seq, int condition);
#define ANNOTATE_BELIEF(roles, level, cond, rate) do{  \
	static int my_seq = -1;              \
	if (my_seq == -1) { my_seq = annotate_belief_seq++; ANNOTATE_BELIEF_FIRST(my_seq, rate, #cond, __FILE__, __LINE__); }   \
	REAL_ANNOTATE_BELIEF(roles, level, my_seq, cond);  \
}while(0)
extern int annotate_belief_seq;

#ifdef __cplusplus
}
#endif

#endif
