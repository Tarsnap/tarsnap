#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include "warnp.h"

#include "tsnetwork.h"

struct network_connect_cookie {
	int s;
	int failed;
	int errnum;
	network_callback * callback;
	void * cookie;
};

static network_callback callback_connect;

/**
 * callback_connect(cookie, status):
 * Callback helper for tsnetwork_connect.
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
		if (C->failed) {
			/*
			 * The connect() call returned an error; restore the
			 * errno value from that point so that when we invoke
			 * our callback we have the right value there.
			 */
			status = NETWORK_STATUS_CONNERR;
			errno = C->errnum;
		} else {
			/*
			 * There was a connection timeout; set errno to zero
			 * here since any value in errno at this point is
			 * just left over from a previous failed syscall.
			 */
			status = NETWORK_STATUS_CTIMEOUT;
			errno = 0;
		}
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
 * tsnetwork_connect(s, addr, addrlen, timeout, callback, cookie):
 * Connect the specified socket to the specified address, and call the
 * specified callback when connected or the connection attempt has failed.
 */
int
tsnetwork_connect(int s, const struct sockaddr * addr, socklen_t addrlen,
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
#ifdef FREEBSD_PORTRANGE_BUG
	/**
	 * If FreeBSD's net.inet.ip.portrange.randomized sysctl is set to 1
	 * (the default value) FreeBSD sometimes reuses a source port faster
	 * than might naively be expected.  This doesn't cause any problems
	 * except if the pf firewall is running on the source system; said
	 * firewall detects the packet as belonging to an expired connection
	 * and drops it.  This would be fine, except that the FreeBSD kernel
	 * doesn't merely drop the packet when a firewall blocks an outgoing
	 * packet; instead, it reports EPERM back to the userland process
	 * which was responsible for the packet being sent.
	 * In short, things interact in wacky ways which make it possible to
	 * get EPERM back in response to a connect(2) syscall.  Work around
	 * this by handling EPERM the same way as transient network glitches;
	 * the upstream code will handle this appropriately by retrying the
	 * connection, at which point a new source port number will be chosen
	 * and everything will (probably) work fine.
	 */
	    (errno == EPERM) ||
#endif
	    (errno == EHOSTUNREACH)) {
		/*
		 * The connection attempt has failed.  Schedule a callback
		 * to be performed after we return, since we're not allowed
		 * to perform the callback right now.
		 */
		C->failed = 1;
		C->errnum = errno;
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
