#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "events.h"
#include "mpool.h"
#include "warnp.h"

#include "network.h"

/**
 * POSIX.1-2008 requires that MSG_NOSIGNAL be defined as a flag for send(2)
 * which has the effect of preventing SIGPIPE from being raised when writing
 * to a descriptor which has been shut down.  Unfortunately there are some
 * platforms which are not POSIX.1-2008 compliant; we provide a workaround
 * (-DPOSIXFAIL_MSG_NOSIGNAL) which instead blocks the SIGPIPE signal on such
 * platforms.
 *
 * (This workaround could be used automatically, but requiring that it be
 * explicitly enabled helps to get platforms fixed.)
 */
#ifdef POSIXFAIL_MSG_NOSIGNAL
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

struct network_write_cookie {
	int (*callback)(void *, ssize_t);
	void * cookie;
	int fd;
	const uint8_t * buf;
	size_t buflen;
	size_t minlen;
	size_t bufpos;
};

MPOOL(network_write_cookie, struct network_write_cookie, 16);

/* Invoke the callback, clean up, and return the callback's status. */
static int
docallback(struct network_write_cookie * C, ssize_t nbytes)
{
	int rc;

	/* Invoke the callback. */
	rc = (C->callback)(C->cookie, nbytes);

	/* Clean up. */
	mpool_network_write_cookie_free(C);

	/* Return the callback's status. */
	return (rc);
}

/* The socket is ready for reading/writing. */
static int
callback_buf(void * cookie)
{
	struct network_write_cookie * C = cookie;
	size_t oplen;
	ssize_t len;
#ifdef POSIXFAIL_MSG_NOSIGNAL
	void (*oldsig)(int);
#endif

	/* If we don't have MSG_NOSIGNAL, catch SIGPIPE. */
#ifdef POSIXFAIL_MSG_NOSIGNAL
	if ((oldsig = signal(SIGPIPE, SIG_IGN)) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		goto failed;
	}
#endif

	/* Attempt to read/write data to/from the buffer. */
	oplen = C->buflen - C->bufpos;
	len = send(C->fd, C->buf + C->bufpos, oplen, MSG_NOSIGNAL);

	/* We should never see a send length of zero. */
	assert(len != 0);

	/* If we set a SIGPIPE handler, restore the old one. */
#ifdef POSIXFAIL_MSG_NOSIGNAL
	if (signal(SIGPIPE, oldsig) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		goto failed;
	}
#endif

	/* Failure? */
	if (len == -1) {
		/* Was it really an error, or just a try-again? */
		if ((errno == EAGAIN) ||
		    (errno == EWOULDBLOCK) ||
		    (errno == EINTR))
			goto tryagain;

		/* Something went wrong. */
		goto failed;
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
	    EVENTS_NETWORK_OP_WRITE))
		goto failed;

	/* Callback was reset. */
	return (0);

failed:
	/* Invoke the callback with a failure status and return. */
	return (docallback(C, -1));
}

/**
 * network_write(fd, buf, buflen, minwrite, callback, cookie):
 * Asynchronously write up to ${buflen} bytes of data from ${buf} to ${fd}.
 * When at least ${minwrite} bytes have been written or on error, invoke
 * ${callback}(${cookie}, lenwrit), where lenwrit is -1 on error and the
 * number of bytes written (between ${minwrite} and ${buflen} inclusive)
 * otherwise.  Return a cookie which can be passed to network_write_cancel in
 * order to cancel the write.
 */
void *
network_write(int fd, const uint8_t * buf, size_t buflen, size_t minwrite,
    int (* callback)(void *, ssize_t), void * cookie)
{
	struct network_write_cookie * C;

	/* Make sure buflen is non-zero. */
	assert(buflen != 0);

	/* Sanity-check: # bytes must fit into a ssize_t. */
	assert(buflen <= SSIZE_MAX);

	/* Bake a cookie. */
	if ((C = mpool_network_write_cookie_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->fd = fd;
	C->buf = buf;
	C->buflen = buflen;
	C->minlen = minwrite;
	C->bufpos = 0;

	/* Register a callback for network readiness. */
	if (events_network_register(callback_buf, C, C->fd,
	    EVENTS_NETWORK_OP_WRITE))
		goto err1;

	/* Success! */
	return (C);

err1:
	mpool_network_write_cookie_free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_write_cancel(cookie):
 * Cancel the buffer write for which the cookie ${cookie} was returned by
 * network_write.  Do not invoke the callback associated with the write.
 */
void
network_write_cancel(void * cookie)
{
	struct network_write_cookie * C = cookie;

	/* Kill the network event. */
	events_network_cancel(C->fd, EVENTS_NETWORK_OP_WRITE);

	/* Free the cookie. */
	mpool_network_write_cookie_free(C);
}
