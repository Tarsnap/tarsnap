#ifndef _NETPROTO_H_
#define _NETPROTO_H_

#include <stddef.h>
#include <stdint.h>

#include "warnp.h"

#include "tsnetwork.h"

typedef struct netproto_connection_internal NETPROTO_CONNECTION;

/* callback status values. */
#define NETPROTO_STATUS_PROTERR	\
    (NETWORK_STATUS_MAX + 1)		/* Protocol error. */

/**
 * netproto_printerr_internal(status):
 * Print the error message associated with the given status code.
 */
void netproto_printerr_internal(int);

#define netproto_printerr(x)	do {	\
	warnline;			\
	netproto_printerr_internal(x);	\
} while (0)

/**
 * netproto_connect(useragent, callback, cookie):
 * Create a socket, connect to the tarsnap server, and perform the necessary
 * key exchange.  Return a network protocol connection cookie; note that
 * this cookie must not be used until the callback is called.
 */
NETPROTO_CONNECTION * netproto_connect(const char *,
    network_callback *, void *);

/**
 * netproto_writepacket(C, type, buf, buflen, callback, cookie):
 * Write the provided packet to the connection.  When complete, call
 * callback(cookie, status), where status is a NETPROTO_STATUS_* value.
 */
int netproto_writepacket(NETPROTO_CONNECTION *, uint8_t, const uint8_t *,
    size_t, network_callback *, void *);

/**
 * netproto_readpacket(C, callback_getbuf, callback_done, cookie):
 * Read a packet from the connection.  Once the type and length of the
 * packet is known, call callback_getbuf(cookie, type, buf, buflen); once
 * the packet is read or fails, call callback_done(cookie, status), where
 * status is a NETPROTO_STATUS_* value.
 */
int netproto_readpacket(NETPROTO_CONNECTION *,
    int(void *, uint8_t, uint8_t **, size_t),
    network_callback *, void *);

/**
 * netproto_getstats(C, in, out, queued):
 * Obtain the number of bytes received and sent via the connection, and the
 * number of bytes ${queued} to be written.
 */
void netproto_getstats(NETPROTO_CONNECTION *, uint64_t *, uint64_t *,
    uint64_t *);

/**
 * netproto_sleep(C, secs, callback, cookie):
 * Call the provided callback after ${secs} seconds.
 */
int netproto_sleep(NETPROTO_CONNECTION *, int, network_callback *, void *);

/**
 * netproto_flush(C):
 * Cancel all pending writes and any in-progress read.
 */
int netproto_flush(NETPROTO_CONNECTION *);

/**
 * netproto_close(C):
 * Cancel all pending writes and any in-progress read, and free memory.
 */
int netproto_close(NETPROTO_CONNECTION *);

#endif /* !_NETPROTO_H_ */
