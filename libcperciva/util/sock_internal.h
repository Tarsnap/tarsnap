#ifndef SOCK_INTERNAL_H_
#define SOCK_INTERNAL_H_

#include <sys/socket.h>

/* Socket address structure. */
struct sock_addr {
	int ai_family;
	int ai_socktype;
	struct sockaddr * name;
	socklen_t namelen;
};

#endif /* !SOCK_INTERNAL_H_ */
