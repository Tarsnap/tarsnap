#ifndef _NETPROTO_INTERNAL_H_
#define _NETPROTO_INTERNAL_H_

#include <stdint.h>

#include "crypto.h"
#include "network.h"

struct netproto_connection_internal {
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
 * netproto_open(fd):
 * Create and return a network protocol connection cookie for use on the
 * connected socket ${fd}.  Note that netproto_keyexchange() must be called
 * before _writepacket or _readpacket are called on the cookie.
 */
struct netproto_connection_internal * netproto_open(int);

/**
 * netproto_keyexchange(C, callback, cookie):
 * Perform protocol negotiation and key exchange with the tarsnap server
 * on the newly opened connection with cookie ${C}.  When the negotiation
 * is complete or has failed, call callback(cookie, status) where status is
 * a NETPROTO_STATUS_* value.
 */
int netproto_keyexchange(struct netproto_connection_internal *,
    network_callback *, void *);

#endif /* !_NETPROTO_INTERNAL_H_ */
