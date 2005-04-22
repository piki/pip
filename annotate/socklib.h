#ifndef SOCKLIB_H
#define SOCKLIB_H

#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

int resolve(const char *server, struct in_addr *addr);
int sock_connect(const char *host, unsigned short port);

#ifdef __cplusplus
}
#endif

#endif
