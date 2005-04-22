#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "socklib.h"

int resolve(const char *server, struct in_addr *addr) {
  struct hostent *hent;

  if (inet_aton(server, addr) != 0) return 0;
  hent = gethostbyname(server);
  if (!hent) {
		herror(server);
		return -1;
	}
  memcpy(addr, hent->h_addr_list[0], sizeof(*addr));
  return 0;
}

int sock_connect(const char *host, unsigned short port) {
  int fd;
  struct in_addr addr;
  struct sockaddr_in sock;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return -1;
  }
  if (resolve(host, &addr) < 0) {
    fprintf(stderr, "Cannot resolve %s\n", host);
    return -1;
  }
  bzero(&sock, sizeof(sock));
  sock.sin_family = AF_INET;
  sock.sin_addr = addr;
  sock.sin_port = htons(port);
  if (connect(fd, (struct sockaddr*)&sock, sizeof(sock)) == -1) {
		perror("connect");
		close(fd);
		return -1;
  }

  return fd;
}
