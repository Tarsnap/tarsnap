#ifndef _NETPROTO_INTERNAL_H_
#define _NETPROTO_INTERNAL_H_

#include <stdint.h>

#include "crypto.h"
#include "tsnetwork.h"

struct netproto_connection_internal {
	int (*cancel)(void *);
	void * cookie;
	int fd;
	NETWORK_WRITEQ * Q;
	CRYPTO_SESSION * keys;
	struct sleepcookie {
		int handle;
		network_callback * callback;
		void * cookie;
	} sleepcookie;
	uint64_t bytesin;
	uint64_t bytesout;
	uint64_t bytesqueued;
	int broken;
};

/**
 * netproto_alloc(callback, cookie):
 * Allocate a network protocol connection cookie.  If the connection is closed
 * before netproto_setfd is called, netproto_close will call callback(cookie)
 * in lieu of performing callback cancels on a socket.
 */
struct netproto_connection_internal * netproto_alloc(int (*)(void *), void *);

/**
 * netproto_setfd(C, fd):
 * Set the network protocol connection cookie ${C} to use connected socket
 * ${fd}.  This function must be called exactly once after netproto_alloc
 * before calling any other functions aside from netproto_free.
 */
int netproto_setfd(struct netproto_connection_internal *, int);

/**
 * netproto_open(fd):
 * Create and return a network protocol connection cookie for use on the
 * connected socket ${fd}.  Note that netproto_keyexchange() must be called
 * before _writepacket or _readpacket are called on the cookie.
 */
struct netproto_connection_internal * netproto_open(int);

/**
 * netproto_keyexchange(C, useragent, callback, cookie):
 * Perform protocol negotiation and key exchange with the tarsnap server
 * on the newly opened connection with cookie ${C}.  When the negotiation
 * is complete or has failed, call callback(cookie, status) where status is
 * a NETPROTO_STATUS_* value.
 */
int netproto_keyexchange(struct netproto_connection_internal *, const char *,
    network_callback *, void *);

#endif /* !_NETPROTO_INTERNAL_H_ */
