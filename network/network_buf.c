#include "bsdtar_platform.h"

#include <sys/socket.h>
#include <sys/time.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "network_internal.h"
#include "warnp.h"

#include "network.h"

struct network_buf_cookie {
	network_callback * callback;
	void * cookie;
	int fd;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;
	struct timeval timeout;
	struct timeval timeout_max;
	ssize_t (* sendrecv)(int, void *, size_t, int);
	int netop;
	int flags;
};

static int callback_buf(void * cookie, int timedout);
static int network_buf(int fd, uint8_t * buf, size_t buflen,
    struct timeval * to0, struct timeval * to1,
    network_callback * callback, void * cookie,
    ssize_t (* sendrecv)(int, void *, size_t, int), int netop, int flags);

/**
 * callback_buf(cookie, status):
 * Callback helper for network_read and network_write.
 */
static int
callback_buf(void * cookie, int status)
{
	struct network_buf_cookie * C = cookie;
	struct timeval timeo;
	struct timeval curtime;
	ssize_t len;
	int rc = -1;	/* If not callback or reset, we have an error. */

	if (status != NETWORK_STATUS_OK) {
		/* If we have no data, mark a timeout as "no data" instead. */
		if ((C->bufpos != 0) && (status == NETWORK_STATUS_TIMEOUT))
			status = NETWORK_STATUS_NODATA;
		goto docallback;
	}

	/* Try to read/write data to/from the buffer. */
	if ((len = (C->sendrecv)(C->fd, C->buf + C->bufpos,
	    C->buflen - C->bufpos, C->flags)) == -1) {
		/* If no data is available, reset the callback. */
		if ((errno == EAGAIN) ||
		    (errno == EWOULDBLOCK) ||
		    (errno == EINTR))
			goto tryagain;

		/* An error occurred.  Let the callback handle it. */
		status = NETWORK_STATUS_ERR;
		goto docallback;
	} else if (len == 0) {
		/* Socket has been shut down by remote host. */
		/* This should occur only when receiving, not when sending. */
		status = NETWORK_STATUS_CLOSED;
		goto docallback;
	} else {
		/* Data has been read/written into/from buffer. */
		C->bufpos += len;
		if (C->bufpos == C->buflen) {
			status = NETWORK_STATUS_OK;
			goto docallback;
		}

		/* Fall through to resetting the callback. */
	}

tryagain:
	/* We need more data.  Reset the callback. */
	if (gettimeofday(&curtime, NULL)) {
		warnp("gettimeofday");
		status = NETWORK_STATUS_ERR;
		goto docallback;
	}
	memcpy(&timeo, &C->timeout, sizeof(struct timeval));
	tv_sub(&timeo, &curtime);
	if (tv_lt(&C->timeout_max, &timeo))
		memcpy(&timeo, &C->timeout_max, sizeof(struct timeval)); 
	if (network_register(C->fd, C->netop, &timeo, callback_buf, C)) {
		status = NETWORK_STATUS_ERR;
		goto docallback;
	}

	/* Callback has been reset. */
	return (0);

docallback:
#if 0
	/* If status is NETWORK_STATUS_ERR, print a warning from errno. */
	if (status == NETWORK_STATUS_ERR)
		warnp("Network error");
#endif

	/* Call the user callback. */
	rc = (C->callback)(C->cookie, status);

	/* Free the cookie. */
	free(C);

	/* Return error or value from user callback. */
	return (rc);
}

/**
 * network_buf(fd, buf, buflen, to0, to1, callback, cookie, sendrecv, netop):
 * Asynchronously read/write the provided buffer from/to ${fd}, and call
 * callback(cookie, status) where status is a NETWORK_STATUS_* value.  Time
 * out if no data can be read/writ for a period of time to0, or if the
 * complete buffer has not been read/writ after time to1.
 */
static int
network_buf(int fd, uint8_t * buf, size_t buflen,
    struct timeval * to0, struct timeval * to1,
    network_callback * callback, void * cookie,
    ssize_t (* sendrecv)(int, void *, size_t, int), int netop, int flags)
{
	struct network_buf_cookie * C;
	struct timeval timeo;

	/* Create a cookie to be passed to callback_buf. */
	if ((C = malloc(sizeof(struct network_buf_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->fd = fd;
	C->buf = buf;
	C->buflen = buflen;
	C->bufpos = 0;
	C->sendrecv = sendrecv;
	C->netop = netop;
	C->flags = flags;

	/* Figure out when we should give up waiting. */
	if (gettimeofday(&C->timeout, NULL)) {
		warnp("gettimeofday");
		goto err1;
	}
	tv_add(&C->timeout, to1);

	/* Record the maximum time that any single send/recv will wait. */
	memcpy(&C->timeout_max, to0, sizeof(struct timeval));

	/* Set up the callback. */
	memcpy(&timeo, to1, sizeof(struct timeval));
	if (tv_lt(to0, &timeo))
		memcpy(&timeo, to0, sizeof(struct timeval)); 
	if (network_register(fd, netop, &timeo, callback_buf, C))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * network_read(fd, buf, buflen, to0, to1, callback, cookie):
 * Asynchronously fill the provided buffer with data from ${fd}, and call
 * callback(cookie, status) where status is a NETWORK_STATUS_* value.  Time
 * out if no data can be read for a period of time to0, or if the complete
 * buffer has not been read after time to1.  Note that ${buflen} must be
 * non-zero, since otherwise deadlock would result.
 */
int
network_read(int fd, uint8_t * buf, size_t buflen,
    struct timeval * to0, struct timeval * to1,
    network_callback * callback, void * cookie)
{

	/* Make sure buflen is non-zero. */
	if (buflen == 0) {
		warn0("Cannot read zero-byte buffer");
		return (-1);
	}

	return (network_buf(fd, buf, buflen, to0, to1, callback, cookie,
	    recv, NETWORK_OP_READ, 0));
}

/**
 * network_write(fd, buf, buflen, timeo, callback, cookie):
 * Asynchronously write data from the provided buffer to ${fd}, and call
 * callback(cookie, status) where status is a NETWORK_STATUS_* value.  Time
 * out if no data can be written for a period of time to0, or if the complete
 * buffer has not been written after time to1.  If ${buflen} is zero, the
 * callback will be invoked with a status of NETWORK_STATUS_CLOSED, even if
 * the connection is still open.
 */
int
network_write(int fd, const uint8_t * buf, size_t buflen,
    struct timeval * to0, struct timeval * to1,
    network_callback * callback, void * cookie)
{

	return (network_buf(fd, (uint8_t *)(uintptr_t)buf, buflen,
	    to0, to1, callback, cookie,
	    (ssize_t (*)(int, void *, size_t, int))send, NETWORK_OP_WRITE,
	    MSG_NOSIGNAL));
}
