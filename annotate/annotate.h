#ifndef ANNOTATE_H
#define ANNOTATE_H

void ANNOTATE_INIT(void);
void ANNOTATE_START_TASK(const char *name);
void ANNOTATE_END_TASK(const char *name);
void ANNOTATE_SET_PATH_ID(const void *path_id, int idsz);
void ANNOTATE_END_PATH_ID(const void *path_id, int idsz);
void ANNOTATE_NOTICE(const char *fmt, ...);
void ANNOTATE_SEND(const void *msgid, int idsz, int size);
void ANNOTATE_RECEIVE(const void *msgid, int idsz, int size);

#define ANNOTATE_SET_PATH_ID_INT(n) do{int x=n;ANNOTATE_SET_PATH_ID(&(x), sizeof(x));}while(0)
#define ANNOTATE_END_PATH_ID_INT(n) do{int x=n;ANNOTATE_END_PATH_ID(&(x), sizeof(x));}while(0)
#define ANNOTATE_SEND_INT(n, size) do{int x=n;ANNOTATE_SEND(&(x), sizeof(x), size);}while(0)
#define ANNOTATE_RECEIVE_INT(n, size) do{int x=n;ANNOTATE_RECEIVE(&(x), sizeof(x), size);}while(0)

#endif
