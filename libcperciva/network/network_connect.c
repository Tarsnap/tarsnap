#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "events.h"
#include "sock.h"

#include "network.h"

struct connect_cookie {
	int (* callback)(void *, int);
	void * cookie;
	struct sock_addr * const * sas;
	struct timeval timeo;
	void * cookie_immediate;
	int s;
	int timeo_enabled;
	void * cookie_timeo;
};

static int tryconnect(struct connect_cookie *);

/* Invoke the upstream callback and clean up. */
static int
docallback(void * cookie)
{
	struct connect_cookie * C = cookie;
	int rc;

	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, C->s);

	/* Free the cookie. */
	free(C);

	/* Return status from upstream callback. */
	return (rc);
}

/* An address failed to connect. */
static int
dofailed(struct connect_cookie * C)
{

	/* Close the socket which failed to connect. */
	close(C->s);

	/* We don't have an open socket any more. */
	C->s = -1;

	/* This address didn't work. */
	C->sas++;

	/* Try other addresses until we run out of options. */
	return (tryconnect(C));
}

/* Callback when connect(2) succeeds or fails. */
static int
callback_connect(void * cookie)
{
	struct connect_cookie * C = cookie;
	int sockerr;
	socklen_t sockerrlen = sizeof(int);

	/* Stop waiting for the timer callback. */
	if (C->cookie_timeo != NULL) {
		events_timer_cancel(C->cookie_timeo);
		C->cookie_timeo = NULL;
	}

	/* Did we succeed? */
	if (getsockopt(C->s, SOL_SOCKET, SO_ERROR, &sockerr, &sockerrlen))
		goto err1;
	if (sockerr != 0)
		return (dofailed(C));

	/*
	 * Perform the callback (this can be done here rather than being
	 * scheduled as an immediate callback, as we're already running from
	 * callback context).
	 */
	return (docallback(C));

err1:
	close(C->s);
	free(C);

	/* Fatal error! */
	return (-1);
}

/* Callback when a timer expires. */
static int
callback_timeo(void * cookie)
{
	struct connect_cookie * C = cookie;

	/* We're not waiting for a timer callback any more. */
	C->cookie_timeo = NULL;

	/* This connect attempt failed. */
	return (dofailed(C));
}

/* Try to launch a connection.  Free the cookie on fatal errors. */
static int
tryconnect(struct connect_cookie * C)
{

	/* Try addresses until we find one which doesn't fail immediately. */
	for (; C->sas[0] != NULL; C->sas++) {
		/* Can we try to connect to this address? */
		if ((C->s = sock_connect_nb(C->sas[0])) != -1)
			break;
	}

	/* Did we run out of addresses to try? */
	if (C->sas[0] == NULL)
		goto failed;

	/* If we've been asked to have a timeout, set one. */
	if (C->timeo_enabled) {
		if ((C->cookie_timeo = events_timer_register(callback_timeo,
		    C, &C->timeo)) == NULL)
			goto err1;
	} else {
		C->cookie_timeo = NULL;
	}

	/* Wait until this socket connects or fails to do so. */
	if (events_network_register(callback_connect, C, C->s,
	    EVENTS_NETWORK_OP_WRITE))
		goto err2;

	/* Success! */
	return (0);

failed:
	/* Schedule a callback. */
	if ((C->cookie_immediate =
	    events_immediate_register(docallback, C, 0)) == NULL)
		goto err1;

	/* Failure successfully handled. */
	return (0);

err2:
	if (C->cookie_timeo != NULL)
		events_timer_cancel(C->cookie_timeo);
err1:
	if (C->s != -1)
		close(C->s);
	free(C);

	/* Fatal error. */
	return (-1);
}

/**
 * network_connect(sas, callback, cookie):
 * Iterate through the addresses in ${sas}, attempting to create and connect
 * a non-blocking socket.  Once connected, invoke ${callback}(${cookie}, s)
 * where s is the connected socket; upon fatal error or if there are no
 * addresses remaining to attempt, invoke ${callback}(${cookie}, -1).  Return
 * a cookie which can be passed to network_connect_cancel in order to cancel
 * the connection attempt.
 */
void *
network_connect(struct sock_addr * const * sas,
    int (* callback)(void *, int), void * cookie)
{

	/* Let network_connect_timeo handle this. */
	return (network_connect_timeo(sas, NULL, callback, cookie));
}

/**
 * network_connect_timeo(sas, timeo, callback, cookie):
 * Behave as network_connect, but wait a duration of at most ${timeo} for
 * each address which is being attempted.
 */
void *
network_connect_timeo(struct sock_addr * const * sas,
    const struct timeval * timeo,
    int (* callback)(void *, int), void * cookie)
{
	struct connect_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct connect_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->sas = sas;
	C->cookie_immediate = NULL;
	C->cookie_timeo = NULL;
	C->s = -1;

	/* Do we have a timeout? */
	if (timeo != NULL) {
		memcpy(&C->timeo, timeo, sizeof(struct timeval));
		C->timeo_enabled = 1;
	} else {
		C->timeo_enabled = 0;
	}

	/* Try to connect to the first address. */
	if (tryconnect(C))
		goto err0;

	/* Success! */
	return (C);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_connect_cancel(cookie):
 * Cancel the connection attempt for which ${cookie} was returned by
 * network_connect.  Do not invoke the associated callback.
 */
void
network_connect_cancel(void * cookie)
{
	struct connect_cookie * C = cookie;

	/* We should have either an immediate callback or a socket. */
	assert((C->cookie_immediate != NULL) || (C->s != -1));
	assert((C->cookie_immediate == NULL) || (C->s == -1));

	/* Cancel any timer. */
	if (C->cookie_timeo != NULL)
		events_timer_cancel(C->cookie_timeo);

	/* Cancel any immediate callback. */
	if (C->cookie_immediate != NULL)
		events_immediate_cancel(C->cookie_immediate);

	/* Close any socket. */
	if (C->s != -1) {
		events_network_cancel(C->s, EVENTS_NETWORK_OP_WRITE);
		close(C->s);
	}

	/* Free the cookie. */
	free(C);
}
