#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <sys/select.h>

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* Opaque address structure. */
struct sock_addr;

/**
 * network_accept(fd, callback, cookie):
 * Asynchronously accept a connection on the socket ${fd}, which must be
 * already marked as listening and non-blocking.  When a connection has been
 * accepted or an error occurs, invoke ${callback}(${cookie}, s) where s is
 * the accepted connection or -1 on error.  Return a cookie which can be
 * passed to network_accept_cancel in order to cancel the accept.
 */
void * network_accept(int, int (*)(void *, int), void *);

/**
 * network_accept_cancel(cookie):
 * Cancel the connection accept for which the cookie ${cookie} was returned
 * by network_accept.  Do not invoke the callback associated with the accept.
 */
void network_accept_cancel(void *);

/**
 * network_connect(sas, callback, cookie):
 * Iterate through the addresses in ${sas}, attempting to create and connect
 * a non-blocking socket.  Once connected, invoke ${callback}(${cookie}, s)
 * where s is the connected socket; upon fatal error or if there are no
 * addresses remaining to attempt, invoke ${callback}(${cookie}, -1).  Return
 * a cookie which can be passed to network_connect_cancel in order to cancel
 * the connection attempt.
 */
void * network_connect(struct sock_addr * const *,
    int (*)(void *, int), void *);

/**
 * network_connect_timeo(sas, timeo, callback, cookie):
 * Behave as network_connect, but wait a duration of at most ${timeo} for
 * each address which is being attempted.
 */
void * network_connect_timeo(struct sock_addr * const *, const struct timeval *,
    int (*)(void *, int), void *);

/**
 * network_connect_cancel(cookie):
 * Cancel the connection attempt for which ${cookie} was returned by
 * network_connect.  Do not invoke the associated callback.
 */
void network_connect_cancel(void *);

/**
 * network_read(fd, buf, buflen, minread, callback, cookie):
 * Asynchronously read up to ${buflen} bytes of data from ${fd} into ${buf}.
 * When at least ${minread} bytes have been read or on error, invoke
 * ${callback}(${cookie}, lenread), where lenread is 0 on EOF or -1 on error,
 * and the number of bytes read (between ${minread} and ${buflen} inclusive)
 * otherwise.  Return a cookie which can be passed to network_read_cancel in
 * order to cancel the read.
 */
void * network_read(int, uint8_t *, size_t, size_t,
    int (*)(void *, ssize_t), void *);

/**
 * network_read_cancel(cookie):
 * Cancel the buffer read for which the cookie ${cookie} was returned by
 * network_read.  Do not invoke the callback associated with the read.
 */
void network_read_cancel(void *);

/**
 * network_write(fd, buf, buflen, minwrite, callback, cookie):
 * Asynchronously write up to ${buflen} bytes of data from ${buf} to ${fd}.
 * When at least ${minwrite} bytes have been written or on error, invoke
 * ${callback}(${cookie}, lenwrit), where lenwrit is -1 on error and the
 * number of bytes written (between ${minwrite} and ${buflen} inclusive)
 * otherwise.  Return a cookie which can be passed to network_write_cancel in
 * order to cancel the write.
 */
void * network_write(int, const uint8_t *, size_t, size_t,
    int (*)(void *, ssize_t), void *);

/**
 * network_write_cancel(cookie):
 * Cancel the buffer write for which the cookie ${cookie} was returned by
 * network_write.  Do not invoke the callback associated with the write.
 */
void network_write_cancel(void *);

#endif /* !_NETWORK_H_ */
