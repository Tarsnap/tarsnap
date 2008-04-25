#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include "warnp.h"

#include "network.h"

struct network_connect_cookie {
	int s;
	int failed;
	network_callback * callback;
	void * cookie;
};

static network_callback callback_connect;

/**
 * callback_connect(cookie, status):
 * Callback helper for network_connect.
 */
static int
callback_connect(void * cookie, int status)
{
	struct network_connect_cookie * C = cookie;
	int sockerr;
	socklen_t sockerrlen = sizeof(int);
	int rc;

	/*
	 * A timeout here is either a connection timeout or a connection
	 * error, depending upon whether we got here as a result of a zero
	 * second timeout used to postpone handling of a connect() failure.
	 */
	if (status == NETWORK_STATUS_TIMEOUT) {
		if (C->failed)
			status = NETWORK_STATUS_CONNERR;
		else
			status = NETWORK_STATUS_CTIMEOUT;
	}

	if (status != NETWORK_STATUS_OK)
		goto docallback;

	/* Even if the status is ok, we need to check for pending error. */
	if (getsockopt(C->s, SOL_SOCKET, SO_ERROR, &sockerr, &sockerrlen)) {
		status = NETWORK_STATUS_CONNERR;
		goto docallback;
	}
	if (sockerr != 0) {
		errno = sockerr;
		status = NETWORK_STATUS_CONNERR;
	}

docallback:
	/* Call the upstream callback. */
	rc = (C->callback)(C->cookie, status);

	/* Free the cookie. */
	free(C);

	/* Return value from user callback. */
	return (rc);
}

/**
 * network_connect(s, addr, addrlen, timeout, callback, cookie):
 * Connect the specified socket to the specified address, and call the
 * specified callback when connected or the connection attempt has failed.
 */
int
network_connect(int s, const struct sockaddr * addr, socklen_t addrlen,
    struct timeval * timeout, network_callback * callback, void * cookie)
{
	struct network_connect_cookie * C;
	struct timeval timeo;
	int rc = 0;	/* Success unless specified otherwise. */

	/* Create a cookie to be passed to callback_connect. */
	if ((C = malloc(sizeof(struct network_connect_cookie))) == NULL)
		goto err0;
	C->s = s;
	C->failed = 0;
	C->callback = callback;
	C->cookie = cookie;

	/* Mark socket as non-blocking. */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
		warnp("fcntl(O_NONBLOCK)");
		goto err1;
	}

	/* Try to connect to the server. */
	if ((connect(s, addr, addrlen) == 0) ||
	    (errno == EINPROGRESS) ||
	    (errno == EINTR)) {
		/* Connection is being established. */
		if (network_register(s, NETWORK_OP_WRITE, timeout,
		    callback_connect, C))
			goto err1;
	} else if ((errno == ECONNREFUSED) ||
	    (errno == ECONNRESET) ||
	    (errno == ENETDOWN) ||
	    (errno == ENETUNREACH) ||
	    (errno == EHOSTUNREACH)) {
		/*
		 * The connection attempt has failed.  Schedule a callback
		 * to be performed after we return, since we're not allowed
		 * to perform the callback right now.
		 */
		C->failed = 1;
		timeo.tv_sec = timeo.tv_usec = 0;
		if (network_sleep(&timeo, callback_connect, C))
			goto err1;
	} else {
		/* Something went seriously wrong. */
		warnp("Network connection failure");
		goto err1;
	}

	/* Return success or the status from the callback. */
	return (rc);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}
