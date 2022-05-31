#ifndef _TSNETWORK_H_
#define _TSNETWORK_H_

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <stdint.h>

typedef struct network_writeq_internal NETWORK_WRITEQ;
typedef int network_callback(void *, int);

/* "op" parameters to network_register. */
#define	NETWORK_OP_READ		0
#define	NETWORK_OP_WRITE	1

/* callback status values. */
#define NETWORK_STATUS_CONNERR	(-2)	/* Error connecting to server. */
#define NETWORK_STATUS_ERR	(-1)	/* Error from recv/send. */
#define NETWORK_STATUS_OK	0	/* Success. */
#define NETWORK_STATUS_TIMEOUT	1	/* Timeout. */
#define NETWORK_STATUS_NODATA	2	/* Timeout with empty buffer. */
#define NETWORK_STATUS_CTIMEOUT	3	/* Timeout connecting to server. */
#define NETWORK_STATUS_CLOSED	4	/* Connection was closed. */
#define NETWORK_STATUS_CANCEL	5	/* Operation cancelled. */
#define NETWORK_STATUS_ZEROBYTE	6	/* Zero-byte write via writeq. */
#define NETWORK_STATUS_MAX	6

/**
 * network_bwlimit(down, up):
 * Set the bandwidth rate limit to ${down} bytes per second of read bandwidth
 * and ${up} bytes per second of write bandwidth.  The values ${down} and
 * ${up} must be between 8000 and 10^9.
 */
void network_bwlimit(double, double);

/**
 * network_register(fd, op, timeo, callback, cookie):
 * Register a callback to be performed by network_select when file descriptor
 * ${fd} is ready for operation ${op}, or once the timeout has expired.
 */
int network_register(int, int, struct timeval *, network_callback *, void *);

/**
 * network_deregister(fd, op):
 * Deregister the callback, if any, for operation ${op} on descriptor ${fd}.
 * The callback will be called with a status of NETWORK_STATUS_CANCEL.
 */
int network_deregister(int, int);

/**
 * network_sleep(timeo, callback, cookie):
 * Register a callback to be performed by network_select once the specified
 * timeout has expired.  Return a handle which can be passed to
 * network_desleep().
 */
int network_sleep(struct timeval *, network_callback *, void *);

/**
 * network_desleep(handle):
 * Deregister the callback associated with the provided handle.  The
 * callback will be called with a status of NETWORK_STATUS_CANCEL.
 */
int network_desleep(int);

/**
 * network_select(blocking):
 * Call select(2) on file descriptors provided via network_register and make
 * callbacks as appropriate (including timeouts).  Callbacks are deregistered
 * before being performed.  If ${blocking} is non-zero, allow select(2) to
 * block waiting for descriptors and timeouts.  Stop performing callbacks and
 * return the first non-zero value returned by a callback if one occurs.
 */
int network_select(int);

/**
 * network_spin(done):
 * Repeatedly call network_select until either an error occurs or the value
 * pointed to by ${done} is non-zero.
 */
int network_spin(int *);

/**
 * tsnetwork_connect(s, addr, addrlen, timeout, callback, cookie):
 * Connect the specified socket to the specified address, and call the
 * specified callback when connected or the connection attempt has failed.
 */
int tsnetwork_connect(int, const struct sockaddr *, socklen_t,
    struct timeval *, network_callback *, void *);

/**
 * tsnetwork_read(fd, buf, buflen, to0, to1, callback, cookie):
 * Asynchronously fill the provided buffer with data from ${fd}, and call
 * callback(cookie, status) where status is a NETWORK_STATUS_* value.  Time
 * out if no data can be read for a period of time to0, or if the complete
 * buffer has not been read after time to1.  Note that ${buflen} must be
 * non-zero, since otherwise deadlock would result.
 */
int tsnetwork_read(int, uint8_t *, size_t, struct timeval *, struct timeval *,
    network_callback *, void *);

/**
 * tsnetwork_write(fd, buf, buflen, to0, to1, callback, cookie):
 * Asynchronously write data from the provided buffer to ${fd}, and call
 * callback(cookie, status) where status is a NETWORK_STATUS_* value.  Time
 * out if no data can be written for a period of time to0, or if the complete
 * buffer has not been written after time to1.  If ${buflen} is zero, the
 * callback will be invoked with a status of NETWORK_STATUS_CLOSED, even if
 * the connection is still open.
 */
int tsnetwork_write(int, const uint8_t *, size_t, struct timeval *,
    struct timeval *, network_callback *, void *);

/**
 * network_writeq_init(fd):
 * Construct a queue to be used for writing data to ${fd}.
 */
NETWORK_WRITEQ * network_writeq_init(int);

/**
 * network_writeq_add_internal(Q, buf, buflen, timeo, callback, cookie,
 *     abstimeo):
 * Add a buffer write to the specified write queue.  The callback function
 * will be called when the write is finished, fails, or is cancelled.
 * If ${abstimeo} is zero, the timeout is relative to when the buffer in
 * question starts to be written (i.e., when the previous buffered write
 * finishes); otherwise, the timeout is relative to the present time.  If
 * ${buflen} is zero, the callback will be performed, at the appropriate
 * point, with a status of NETWORK_STATUS_ZEROBYTE.
 */
int network_writeq_add_internal(NETWORK_WRITEQ *, const uint8_t *, size_t,
    struct timeval *, network_callback *, void *, int);
#define network_writeq_add(Q, buf, buflen, timeo, callback, cookie)	\
	network_writeq_add_internal(Q, buf, buflen, timeo, callback, cookie, 0)
#define network_writeq_add_abs(Q, buf, buflen, timeo, callback, cookie)	\
	network_writeq_add_internal(Q, buf, buflen, timeo, callback, cookie, 1)

/**
 * network_writeq_cancel(Q):
 * Cancel all queued writes, including any partially completed writes.  Note
 * that since this leaves the connection in an indeterminate state (there is
 * no way to know how much data from the currently in-progress write was
 * written) this should probably only be used prior to closing a connection.
 * The callbacks for each pending write will be called with a status of
 * NETWORK_STATUS_DEQUEUE, and network_writeq_cancel will return the first
 * non-zero value returned by a callback.
 */
int network_writeq_cancel(NETWORK_WRITEQ *);

/**
 * network_writeq_free(Q):
 * Free the specified write queue.  If there might be any pending writes,
 * network_writeq_cancel should be called first.
 */
void network_writeq_free(NETWORK_WRITEQ *);

/**
 * network_getselectstats(N, mu, va, max):
 * Return and zero statistics on the time between select(2) calls.
 */
void network_getselectstats(double *, double *, double *, double *);

/**
 * network_fini(void):
 * Free resources associated with the network subsystem.
 */
void network_fini(void);

#endif /* !_TSNETWORK_H_ */
