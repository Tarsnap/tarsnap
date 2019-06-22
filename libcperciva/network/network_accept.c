#include <sys/socket.h>

#include <errno.h>
#include <stdlib.h>

#include "events.h"

#include "network.h"

struct accept_cookie {
	int (* callback)(void *, int);
	void * cookie;
	int fd;
};

/* Accept the connection and invoke the callback. */
static int
callback_accept(void * cookie)
{
	struct accept_cookie * C = cookie;
	int s;
	int rc;

	/* Attempt to accept a new connection. */
	if ((s = accept(C->fd, NULL, NULL)) == -1) {
		/* If a connection isn't available, reset the callback. */
		if ((errno == EAGAIN) ||
		    (errno == EWOULDBLOCK) ||
		    (errno == ECONNABORTED) ||
		    (errno == EINTR))
			goto tryagain;
	}

	/* Call the upstream callback. */
	rc = (C->callback)(C->cookie, s);

	/* Free the cookie. */
	free(C);

	/* Return status from upstream callback. */
	return (rc);

tryagain:
	/* Reset the callback. */
	return (events_network_register(callback_accept, C, C->fd,
	    EVENTS_NETWORK_OP_READ));
}

/**
 * network_accept(fd, callback, cookie):
 * Asynchronously accept a connection on the socket ${fd}, which must be
 * already marked as listening and non-blocking.  When a connection has been
 * accepted or an error occurs, invoke ${callback}(${cookie}, s) where s is
 * the accepted connection or -1 on error.  Return a cookie which can be
 * passed to network_accept_cancel in order to cancel the accept.
 */
void *
network_accept(int fd, int (* callback)(void *, int), void * cookie)
{
	struct accept_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct accept_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->fd = fd;

	/*
	 * Register a network event.  A connection arriving on a listening
	 * socket is treated by select(2) as the socket becoming readable.
	 */
	if (events_network_register(callback_accept, C, C->fd,
	    EVENTS_NETWORK_OP_READ))
		goto err1;

	/* Success! */
	return (C);

err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_accept_cancel(cookie):
 * Cancel the connection accept for which the cookie ${cookie} was returned
 * by network_accept.  Do not invoke the callback associated with the accept.
 */
void
network_accept_cancel(void * cookie)
{
	struct accept_cookie * C = cookie;

	/* Cancel the network event. */
	events_network_cancel(C->fd, EVENTS_NETWORK_OP_READ);

	/* Free the cookie. */
	free(C);
}
