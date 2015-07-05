#include <sys/select.h>

#include <errno.h>
#include <stdlib.h>

#include "elasticarray.h"
#include "warnp.h"

#include "events_internal.h"
#include "events.h"

/* Structure for holding readability and writability events for a socket. */
struct socketrec {
	struct eventrec * reader;
	struct eventrec * writer;
};

/* List of sockets. */
ELASTICARRAY_DECL(SOCKETLIST, socketlist, struct socketrec);
static SOCKETLIST S = NULL;

/* File descriptor sets containing unevented ready sockets. */
static fd_set readfds;
static fd_set writefds;

/* Position to which events_network_get has scanned in *fds. */
static size_t fdscanpos;

/* Number of registered events. */
static size_t nev;

/* Initialize the socket list if we haven't already done so. */
static int
initsocketlist(void)
{

	/* If we're already initialized, do nothing. */
	if (S != NULL)
		goto done;

	/* Initialize the socket list. */
	if ((S = socketlist_init(0)) == NULL)
		goto err0;

	/* There are no events registered. */
	nev = 0;

	/* There are no unevented ready sockets. */
	fdscanpos = FD_SETSIZE;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Grow the socket list and initialize new records. */
static int
growsocketlist(size_t nrec)
{
	size_t i;

	/* Get the old size. */
	i = socketlist_getsize(S);

	/* Grow the list. */
	if (socketlist_resize(S, nrec))
		goto err0;

	/* Initialize new members. */
	for (; i < nrec; i++) {
		socketlist_get(S, i)->reader = NULL;
		socketlist_get(S, i)->writer = NULL;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_network_register(func, cookie, s, op):
 * Register ${func}(${cookie}) to be run when socket ${s} is ready for
 * reading or writing depending on whether ${op} is EVENTS_NETWORK_OP_READ or
 * EVENTS_NETWORK_OP_WRITE.  If there is already an event registration for
 * this ${s}/${op} pair, errno will be set to EEXIST and the function will
 * fail.
 */
int
events_network_register(int (*func)(void *), void * cookie, int s, int op)
{
	struct eventrec ** r;

	/* Initialize if necessary. */
	if (initsocketlist())
		goto err0;

	/* Sanity-check socket number. */
	if ((s < 0) || (s >= (int)FD_SETSIZE)) {
		warn0("Invalid file descriptor for network event: %d", s);
		goto err0;
	}

	/* Sanity-check operation. */
	if ((op != EVENTS_NETWORK_OP_READ) &&
	    (op != EVENTS_NETWORK_OP_WRITE)) {
		warn0("Invalid operation for network event: %d", op);
		goto err0;
	}

	/* Grow the array if necessary. */
	if (((size_t)(s) >= socketlist_getsize(S)) &&
	    (growsocketlist(s + 1) != 0))
		goto err0;

	/* Look up the relevant event pointer. */
	if (op == EVENTS_NETWORK_OP_READ)
		r = &socketlist_get(S, s)->reader;
	else
		r = &socketlist_get(S, s)->writer;

	/* Error out if we already have an event registered. */
	if (*r != NULL) {
		errno = EEXIST;
		goto err0;
	}

	/* Register the new event. */
	if ((*r = events_mkrec(func, cookie)) == NULL)
		goto err0;

	/*
	 * Increment events-registered counter; and if it was zero, start the
	 * inter-select duration clock.
	 */
	if (nev++ == 0)
		events_network_selectstats_startclock();

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_network_cancel(s, op):
 * Cancel the event registered for the socket/operation pair ${s}/${op}.  If
 * there is no such registration, errno will be set to ENOENT and the
 * function will fail.
 */
int
events_network_cancel(int s, int op)
{
	struct eventrec ** r;

	/* Initialize if necessary. */
	if (initsocketlist())
		goto err0;

	/* Sanity-check socket number. */
	if ((s < 0) || (s >= (int)FD_SETSIZE)) {
		warn0("Invalid file descriptor for network event: %d", s);
		goto err0;
	}

	/* Sanity-check operation. */
	if ((op != EVENTS_NETWORK_OP_READ) &&
	    (op != EVENTS_NETWORK_OP_WRITE)) {
		warn0("Invalid operation for network event: %d", op);
		goto err0;
	}

	/* We have no events registered beyond the end of the array. */
	if ((size_t)(s) >= socketlist_getsize(S)) {
		errno = ENOENT;
		goto err0;
	}

	/* Look up the relevant event pointer. */
	if (op == EVENTS_NETWORK_OP_READ)
		r = &socketlist_get(S, s)->reader;
	else
		r = &socketlist_get(S, s)->writer;

	/* Check if we have an event. */
	if (*r == NULL) {
		errno = ENOENT;
		goto err0;
	}

	/* Free the event. */
	events_freerec(*r);
	*r = NULL;

	/*
	 * Since there is no longer an event registered for this socket /
	 * operation pair, it doesn't make any sense for it to be ready.
	 */
	if (op == EVENTS_NETWORK_OP_READ)
		FD_CLR(s, &readfds);
	else
		FD_CLR(s, &writefds);

	/*
	 * Decrement events-registered counter; and if it is becoming zero,
	 * stop the inter-select duration clock.
	 */
	if (--nev == 0)
		events_network_selectstats_stopclock();

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_network_select(tv):
 * Check for socket readiness events, waiting up to ${tv} time if there are
 * no sockets immediately ready, or indefinitely if ${tv} is NULL.  The value
 * stored in ${tv} may be modified.
 */
int
events_network_select(struct timeval * tv)
{
	size_t i;

	/* Initialize if necessary. */
	if (initsocketlist())
		goto err0;

	/* Zero the fd sets... */
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);

	/* ... and add the ones we care about. */
	for (i = 0; i < socketlist_getsize(S); i++) {
		if (socketlist_get(S, i)->reader)
			FD_SET(i, &readfds);
		if (socketlist_get(S, i)->writer)
			FD_SET(i, &writefds);
	}

	/* We're about to call select! */
	events_network_selectstats_select();

	/* Select. */
	while (select(socketlist_getsize(S), &readfds, &writefds,
	    NULL, tv) == -1) {
		/* EINTR is harmless. */
		if (errno == EINTR)
			continue;

		/* Anything else is an error. */
		warnp("select()");
		goto err0;
	}

	/* If we have any events registered, start the clock again. */
	if (nev > 0)
		events_network_selectstats_startclock();

	/* We should start scanning for events at the beginning. */
	fdscanpos = 0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_network_get(void):
 * Find a socket readiness event which was identified by a previous call to
 * events_network_select, and return it as an eventrec structure; or return
 * NULL if there are no such events available.  The caller is responsible for
 * freeing the returned memory.
 */
struct eventrec *
events_network_get(void)
{
	struct eventrec * r;
	size_t nfds = socketlist_getsize(S);

	/* We haven't found any events yet. */
	r = NULL;

	/* Scan through the fd sets looking for ready sockets. */
	for (; fdscanpos < nfds; fdscanpos++) {
		/* Are we ready for reading? */
		if (FD_ISSET(fdscanpos, &readfds)) {
			r = socketlist_get(S, fdscanpos)->reader;
			socketlist_get(S, fdscanpos)->reader = NULL;
			if (--nev == 0)
				events_network_selectstats_stopclock();
			FD_CLR(fdscanpos, &readfds);
			break;
		}

		/* Are we ready for writing? */
		if (FD_ISSET(fdscanpos, &writefds)) {
			r = socketlist_get(S, fdscanpos)->writer;
			socketlist_get(S, fdscanpos)->writer = NULL;
			if (--nev == 0)
				events_network_selectstats_stopclock();
			FD_CLR(fdscanpos, &writefds);
			break;
		}
	}

	/* Return the event we found, or NULL if we didn't find any. */
	return (r);
}

/**
 * events_network_shutdown(void)
 * Clean up and free memory.  This call is not necessary on program exit and
 * is only expected to be useful when checking for memory leaks.
 */
void
events_network_shutdown(void)
{

	/* If we're not initialized, do nothing. */
	if (S == NULL)
		return;

	/* If we have any registered events, do nothing. */
	if (nev > 0)
		return;

	/* Free the socket list. */
	socketlist_free(S);
	S = NULL;
}
