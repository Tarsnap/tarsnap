#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "events.h"
#include "warnp.h"

#include "network.h"

/**
 * We use three methods to prevent SIGPIPE from being sent, in order of
 * preference:
 * 1. The MSG_NOSIGNAL send(2) flag.
 * 2. The SO_NOSIGPIPE socket option.
 * 3. Blocking the SIGPIPE signal.
 */
#ifdef MSG_NOSIGNAL
#define USE_MSG_NOSIGNAL
#else /* !MSG_NOSIGNAL */
#define MSG_NOSIGNAL 0
#ifdef SO_NOSIGPIPE
#define USE_SO_NOSIGPIPE
#else /* !MSG_NOSIGNAL, !SO_NOSIGPIPE */
#define USE_SIGNAL
#endif /* !MSG_NOSIGNAL, !SO_NOSIGPIPE */
#endif /* !MSG_NOSIGNAL */

struct network_buf_cookie {
	int (*callback)(void *, ssize_t);
	void * cookie;
	int fd;
	uint8_t * buf;
	size_t buflen;
	size_t minlen;
	size_t bufpos;
	ssize_t (* sendrecv)(int, void *, size_t, int);
	int op;
	int flags;
};

static int docallback(struct network_buf_cookie *, size_t);
static int callback_buf(void *);
static struct network_buf_cookie * network_buf(int, uint8_t *, size_t,
    size_t, int (*)(void *, ssize_t), void *,
    ssize_t (*)(int, void *, size_t, int), int, int);
static void cancel(void *);

/* Invoke the callback, clean up, and return the callback's status. */
static int
docallback(struct network_buf_cookie * C, size_t nbytes)
{
	int rc;

	/* Invoke the callback. */
	rc = (C->callback)(C->cookie, nbytes);

	/* Clean up. */
	free(C);

	/* Return the callback's status. */
	return (rc);
}

/* The socket is ready for reading/writing. */
static int
callback_buf(void * cookie)
{
	struct network_buf_cookie * C = cookie;
	size_t oplen;
	ssize_t len;
#ifdef USE_SO_NOSIGPIPE
	int val;
#endif
#ifdef USE_SIGNAL
	void (*oldsig)(int);
#endif

	/* Make sure we don't get a SIGPIPE. */
#ifdef USE_SO_NOSIGPIPE
	val = 1;
	if (setsockopt(C->fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(int))) {
		warnp("setsockopt(SO_NOSIGPIPE)");
		goto failed;
	}
#endif
#ifdef USE_SIGNAL
	if ((oldsig = signal(SIGPIPE, SIG_IGN)) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		goto failed;
	}
#endif

	/* Attempt to read/write data to/from the buffer. */
	oplen = C->buflen - C->bufpos;
	len = (C->sendrecv)(C->fd, C->buf + C->bufpos, oplen, C->flags);

	/* Undo whatever we did to prevent SIGPIPEs. */
#ifdef USE_SO_NOSIGPIPE
	val = 0;
	if (setsockopt(C->fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(int))) {
		warnp("setsockopt(SO_NOSIGPIPE)");
		goto failed;
	}
#endif
#ifdef USE_SIGNAL
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
	} else if (len == 0) {
		/* The socket was shut down by the remote host. */
		goto eof;
	}

	/* We processed some data.  Do we need to keep going? */
	if ((C->bufpos += len) < C->minlen)
		goto tryagain;

	/* Invoke the callback and return. */
	return (docallback(C, C->bufpos));

tryagain:
	/* Reset the event. */
	if (events_network_register(callback_buf, C, C->fd, C->op))
		goto failed;

	/* Callback was reset. */
	return (0);

eof:
	/* Sanity-check: This should only occur for reads. */
	assert(C->op == EVENTS_NETWORK_OP_READ);

	/* Invoke the callback with an EOF status and return. */
	return (docallback(C, 0));

failed:
	/* Invoke the callback with a failure status and return. */
	return (docallback(C, -1));
}

/**
 * network_buf(fd, buf, buflen, minlen, callback, cookie, sendrecv, op, flags):
 * Asynchronously read/write up to ${buflen} bytes of data from/to ${fd}
 * to/from ${buf}.  When at least ${minlen} bytes have been read/written,
 * invoke ${callback}(${cookie}, nbytes), where nbytes is 0 on EOF or -1 on
 * error and the number of bytes read/written (between ${minlen} and ${buflen}
 * inclusive) otherwise.  Return a cookie which can be passed to buf_cancel
 * in order to cancel the read/write.
 */
static struct network_buf_cookie *
network_buf(int fd, uint8_t * buf, size_t buflen, size_t minlen,
    int (* callback)(void *, ssize_t), void * cookie,
    ssize_t (* sendrecv)(int, void *, size_t, int), int op, int flags)
{
	struct network_buf_cookie * C;

	/* Sanity-check: # bytes must fit into a ssize_t. */
	assert(buflen <= SSIZE_MAX);

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct network_buf_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->fd = fd;
	C->buf = buf;
	C->buflen = buflen;
	C->minlen = minlen;
	C->bufpos = 0;
	C->sendrecv = sendrecv;
	C->op = op;
	C->flags = flags;

	/* Register a callback for network readiness. */
	if (events_network_register(callback_buf, C, C->fd, C->op))
		goto err1;

	/* Success! */
	return (C);

err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/* Cancel the read/write. */
static void
cancel(void * cookie)
{
	struct network_buf_cookie * C = cookie;

	/* Kill the network event. */
	events_network_cancel(C->fd, C->op);

	/* Free the cookie. */
	free(C);
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

	/* Make sure buflen is non-zero. */
	if (buflen == 0) {
		warn0("Programmer error: Cannot read zero-byte buffer");
		return (NULL);
	}

	/* Get network_buf to set things up for us. */
	return (network_buf(fd, buf, buflen, minread, callback, cookie, recv,
	    EVENTS_NETWORK_OP_READ, 0));
}

/**
 * network_read_cancel(cookie):
 * Cancel the buffer read for which the cookie ${cookie} was returned by
 * network_read.  Do not invoke the callback associated with the read.
 */
void
network_read_cancel(void * cookie)
{

	/* Get cancel to do the work for us. */
	cancel(cookie);
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

	/* Make sure buflen is non-zero. */
	if (buflen == 0) {
		warn0("Programmer error: Cannot write zero-byte buffer");
		return (NULL);
	}

	/* Get network_buf to set things up for us. */
	return (network_buf(fd, (uint8_t *)(uintptr_t)buf, buflen, minwrite,
	    callback, cookie,
	    (ssize_t (*)(int, void *, size_t, int))send,
	    EVENTS_NETWORK_OP_WRITE, MSG_NOSIGNAL));
}

/**
 * network_write_cancel(cookie):
 * Cancel the buffer write for which the cookie ${cookie} was returned by
 * network_write.  Do not invoke the callback associated with the write.
 */
void
network_write_cancel(void * cookie)
{

	/* Get cancel to do the work for us. */
	cancel(cookie);
}
