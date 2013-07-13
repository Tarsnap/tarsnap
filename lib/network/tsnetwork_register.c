#include <sys/select.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "elasticarray.h"
#include "events.h"
#include "warnp.h"

#include "tsnetwork.h"
#include "tsnetwork_internal.h"

/* Callbacks. */
struct callback {
	network_callback * callback;
	void * cookie;
	int s;
	int eop;
	int event_network_pending;
	void * event_timer;
};
ELASTICARRAY_DECL(CALLBACKS, callbacks, struct callback *);
static CALLBACKS callbacks[2] = {NULL, NULL};
static int suspended[2] = {0, 0};
static int eops[2] = {EVENTS_NETWORK_OP_READ, EVENTS_NETWORK_OP_WRITE};

/* Translate an "op" value to a set of callbacks. */
static int
op2cs(int op)
{

	switch (op) {
	case NETWORK_OP_READ:
		return (0);
	case NETWORK_OP_WRITE:
		return (1);
	default:
		assert(!"Invalid operation type in network_register");
	}
}

/* Invoke the callback. */
static int
docallback(struct callback * c, int timedout)
{
	network_callback * cb;
	int status;

	/* Cancel any pending event callbacks. */
	if (c->event_network_pending)
		events_network_cancel(c->s, c->eop);
	if (c->event_timer != NULL)
		events_timer_cancel(c->event_timer);

	/* Grab the callback function and empty the record. */
	cb = c->callback;
	c->callback = NULL;

	/* Translate timeout status to network status. */
	switch (timedout) {
	case -1:
		status = NETWORK_STATUS_CANCEL;
		break;
	case 0:
		status = NETWORK_STATUS_OK;
		break;
	case 1:
		status = NETWORK_STATUS_TIMEOUT;
		break;
	default:
		assert(!"Invalid status to docallback");
		return (-1);
	}

	/* Invoke callback. */
	return ((cb)(c->cookie, status));
}

/* Callback from network_register when timer expires. */
static int
callback_timer(void * cookie)
{
	struct callback * c = cookie;

	/* This callback is no longer pending. */
	c->event_timer = NULL;

	/* Do callback. */
	return (docallback(c, 1));
}

/* Callback from network_register when socket is ready. */
static int
callback_network(void * cookie)
{
	struct callback * c = cookie;

	/* This callback is no longer pending. */
	c->event_network_pending = 0;

	/* Do callback. */
	return (docallback(c, 0));
}

/**
 * network_register(fd, op, timeo, callback, cookie):
 * Register a callback to be performed by network_select when file descriptor
 * ${fd} is ready for operation ${op}, or once the timeout has expired.
 */
int
network_register(int fd, int op, struct timeval * timeo,
    network_callback * callback, void * cookie)
{
	CALLBACKS * csp;
	CALLBACKS cs;
	struct callback * c;
	int osize;
	int eop;

	/* Sanity-check the file descriptor. */
	if ((fd < 0) || (fd >= (int)FD_SETSIZE)) {
		warn0("Invalid file descriptor: %d", fd);
		goto err0;
	}

	/*
	 * Figure out which set of callbacks we're dealing with and which
	 * type of event we need to listen for.
	 */
	csp = &callbacks[op2cs(op)];
	eop = eops[op2cs(op)];

	/* Initialize array if required. */
	if ((cs = *csp) == NULL) {
		if ((cs = *csp = callbacks_init(0)) == NULL)
			goto err0;
	}

	/* Enlarge array if necessary. */
	if ((osize = (int)callbacks_getsize(cs)) <= fd) {
		/* Resize. */
		if (callbacks_resize(cs, fd + 1))
			goto err0;

		/* Initialize empty. */
		for (; osize < fd + 1; osize++) {
			*callbacks_get(cs, osize) = NULL;
		}
	}

	/* Grab the relevant callback record. */
	c = *callbacks_get(cs, fd);

	/* If there is no record in that slot, allocate one. */
	if (c == NULL) {
		/* Allocate. */
		if ((c = malloc(sizeof(struct callback))) == NULL)
			goto err0;

		/* Initialize. */
		c->callback = NULL;
		c->event_network_pending = 0;
		c->event_timer = NULL;

		/* Insert into table slot. */
		*callbacks_get(cs, fd) = c;
	}

	/* Make sure we're not replacing an existing callback. */
	if (c->callback != NULL) {
		warn0("Replacing callback: op = %d, fd = %d", op, fd);
		goto err0;
	}

	/* Set network operation parameters. */
	c->callback = callback;
	c->cookie = cookie;
	c->s = fd;
	c->eop = eop;

	/* Register a timer event. */
	if ((c->event_timer =
	    events_timer_register(callback_timer, c, timeo)) == NULL)
		goto err0;

	/* Register a network event if not suspended. */
	if (suspended[op2cs(op)] == 0) {
		if (events_network_register(callback_network, c, fd, eop))
			goto err1;
		c->event_network_pending = 1;
	} else {
		c->event_network_pending = 0;
	}

	/* Success! */
	return (0);

err1:
	events_timer_cancel(c->event_timer);
	c->event_timer = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * network_deregister(fd, op):
 * Deregister the callback, if any, for operation ${op} on descriptor ${fd}.
 * The callback will be called with a status of NETWORK_STATUS_CANCEL.
 */
int
network_deregister(int fd, int op)
{
	CALLBACKS cs;
	struct callback * c;

	/* Figure out which set of callbacks we're dealing with. */
	cs = callbacks[op2cs(op)];

	/* Sanity-check: We should have a callbacks array. */
	if (cs == NULL) {
		warn0("Callbacks uninitialized");
		goto err0;
	}

	/* Sanity-check the file descriptor. */
	if ((fd < 0) || (fd >= (int)callbacks_getsize(cs))) {
		warn0("Invalid file descriptor: %d", fd);
		goto err0;
	}

	/* Grab the relevant callback record. */
	c = *callbacks_get(cs, fd);

	/* If there is no callback, return silently. */
	if (c == NULL || c->callback == NULL)
		return (0);

	/* Invoke the callback. */
	return (docallback(c, -1));

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_register_suspend(op):
 * Suspend ${op} operations, on all file descriptors.
 */
int
network_register_suspend(int op)
{
	CALLBACKS cs;
	struct callback * c;
	size_t i;

	/* This direction is suspended. */
	suspended[op2cs(op)] = 1;

	/* Look up which set of callbacks we're dealing with. */
	cs = callbacks[op2cs(op)];

	/* If we have no callbacks, return immediately. */
	if (cs == NULL)
		return (0);

	/* Scan through the callbacks... */
	for (i = 0; i < callbacks_getsize(cs); i++) {
		c = *callbacks_get(cs, i);

		/* ... ignoring any which don't actually exist... */
		if ((c == NULL) || (c->callback == NULL))
			continue;

		/* ... but cancelling the rest. */
		if (c->event_network_pending) {
			c->event_network_pending = 0;
			if (events_network_cancel(c->s, c->eop))
				goto err0;
		}
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_register_resume(op):
 * Resume pending ${op} operations, on all file descriptors.
 */
int
network_register_resume(int op)
{
	CALLBACKS cs;
	struct callback * c;
	size_t i;

	/* Not suspended any more. */
	suspended[op2cs(op)] = 0;

	/* Look up which set of callbacks we're dealing with. */
	cs = callbacks[op2cs(op)];

	/* If we have no callbacks, return immediately. */
	if (cs == NULL)
		return (0);

	/* Scan through the callbacks... */
	for (i = 0; i < callbacks_getsize(cs); i++) {
		c = *callbacks_get(cs, i);

		/* ... ignoring any which don't actually exist... */
		if ((c == NULL) || (c->callback == NULL))
			continue;

		/* ... but starting the rest. */
		if (c->event_network_pending == 0) {
			c->event_network_pending = 1;
			if (events_network_register(callback_network,
			    c, c->s, c->eop))
				goto err0;
		}
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_register_fini(void):
 * Free resources allocated.
 */
void
network_register_fini(void)
{
	struct callback * c;
	size_t i, j;

	/* For each direction... */
	for (i = 0; i < 2; i++) {
		/* Skip uninitialized directions. */
		if (callbacks[i] == NULL)
			continue;

		/* For each descriptor... */
		for (j = 0; j < callbacks_getsize(callbacks[i]); j++) {
			c = *callbacks_get(callbacks[i], j);

			/* Skip records which don't exist. */
			if (c == NULL)
				continue;

			/* If we have no callback, free the record. */
			if (c->callback == NULL)
				free(c);
		}

		/* Free the callbacks array. */
		callbacks_free(callbacks[i]);
	}
}
