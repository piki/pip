#ifndef ANNOTATE_H
#define ANNOTATE_H

void ANNOTATE_INIT(void);
void ANNOTATE_START_TASK(const char *name);
void ANNOTATE_END_TASK(const char *name);
void ANNOTATE_SET_PATH_ID(unsigned int path_id);
void ANNOTATE_END_PATH_ID(unsigned int path_id);
void ANNOTATE_NOTICE(const char *fmt, ...);
void ANNOTATE_SEND(int sender, int msgid, int size);
void ANNOTATE_RECEIVE(int sender, int msgid, int size);

#endif
