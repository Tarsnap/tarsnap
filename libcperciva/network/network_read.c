#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "events.h"
#include "mpool.h"

#include "network.h"

struct network_read_cookie {
	int (*callback)(void *, ssize_t);
	void * cookie;
	int fd;
	uint8_t * buf;
	size_t buflen;
	size_t minlen;
	size_t bufpos;
};

MPOOL(network_read_cookie, struct network_read_cookie, 16);

/* Invoke the callback, clean up, and return the callback's status. */
static int
docallback(struct network_read_cookie * C, ssize_t nbytes)
{
	int rc;

	/* Invoke the callback. */
	rc = (C->callback)(C->cookie, nbytes);

	/* Clean up. */
	mpool_network_read_cookie_free(C);

	/* Return the callback's status. */
	return (rc);
}

/* The socket is ready for reading/writing. */
static int
callback_buf(void * cookie)
{
	struct network_read_cookie * C = cookie;
	size_t oplen;
	ssize_t len;

	/* Attempt to read/write data to/from the buffer. */
	oplen = C->buflen - C->bufpos;
	len = recv(C->fd, C->buf + C->bufpos, oplen, 0);

	/* Failure? */
	if (len == -1) {
		/* Was it really an error, or just a try-again? */
		if ((errno == EAGAIN) ||
		    (errno == EWOULDBLOCK) ||
		    (errno == EINTR))
			goto tryagain;

		/* Something went wrong. */
		goto failed;
	} else if (len == 0) {
		/* The socket was shut down by the remote host. */
		goto eof;
	}

	/* We processed some data. */
	C->bufpos += (size_t)len;

	/* Do we need to keep going? */
	if (C->bufpos < C->minlen)
		goto tryagain;

	/* Sanity-check: buffer position must fit into a ssize_t. */
	assert(C->bufpos <= SSIZE_MAX);

	/* Invoke the callback and return. */
	return (docallback(C, (ssize_t)C->bufpos));

tryagain:
	/* Reset the event. */
	if (events_network_register(callback_buf, C, C->fd,
	    EVENTS_NETWORK_OP_READ))
		goto failed;

	/* Callback was reset. */
	return (0);

eof:
	/* Invoke the callback with an EOF status and return. */
	return (docallback(C, 0));

failed:
	/* Invoke the callback with a failure status and return. */
	return (docallback(C, -1));
}

/**
 * network_read(fd, buf, buflen, minread, callback, cookie):
 * Asynchronously read up to ${buflen} bytes of data from ${fd} into ${buf}.
 * When at least ${minread} bytes have been read or on error, invoke
 * ${callback}(${cookie}, lenread), where lenread is 0 on EOF or -1 on error,
 * and the number of bytes read (between ${minread} and ${buflen} inclusive)
 * otherwise.  Return a cookie which can be passed to network_read_cancel in
 * order to cancel the read.
 */
void *
network_read(int fd, uint8_t * buf, size_t buflen, size_t minread,
    int (* callback)(void *, ssize_t), void * cookie)
{
	struct network_read_cookie * C;

	/* Make sure buflen is non-zero. */
	assert(buflen != 0);

	/* Sanity-check: # bytes must fit into a ssize_t. */
	assert(buflen <= SSIZE_MAX);

	/* Bake a cookie. */
	if ((C = mpool_network_read_cookie_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->fd = fd;
	C->buf = buf;
	C->buflen = buflen;
	C->minlen = minread;
	C->bufpos = 0;

	/* Register a callback for network readiness. */
	if (events_network_register(callback_buf, C, C->fd,
	    EVENTS_NETWORK_OP_READ))
		goto err1;

	/* Success! */
	return (C);

err1:
	mpool_network_read_cookie_free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_read_cancel(cookie):
 * Cancel the buffer read for which the cookie ${cookie} was returned by
 * network_read.  Do not invoke the callback associated with the read.
 */
void
network_read_cancel(void * cookie)
{
	struct network_read_cookie * C = cookie;

	/* Kill the network event. */
	events_network_cancel(C->fd, EVENTS_NETWORK_OP_READ);

	/* Free the cookie. */
	mpool_network_read_cookie_free(C);
}
