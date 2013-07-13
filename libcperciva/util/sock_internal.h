#ifndef _SOCK_INTERNAL_H_
#define _SOCK_INTERNAL_H_

#include <sys/socket.h>

/* Socket address structure. */
struct sock_addr {
	int ai_family;
	int ai_socktype;
	struct sockaddr * name;
	socklen_t namelen;
};

#endif /* !_SOCK_INTERNAL_H_ */
