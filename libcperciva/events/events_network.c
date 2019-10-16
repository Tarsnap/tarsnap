#include <sys/select.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ctassert.h"
#include "elasticarray.h"
#include "warnp.h"

#include "events.h"
#include "events_internal.h"

/*
 * Sanity checks on the nfds_t type: POSIX simply says "an unsigned integer
 * type used for the number of file descriptors", but it doesn't make sense
 * for it to be larger than size_t unless there's an undocumented limit on
 * the number of descriptors which can be polled (since poll takes an array,
 * the size of which must fit into a size_t); and nfds_t should be able to
 * the value INT_MAX + 1 (in case every possible file descriptor is in use
 * and being polled for).
 */
CTASSERT((nfds_t)(-1) <= (size_t)(-1));
CTASSERT((nfds_t)((size_t)(INT_MAX) + 1) == (size_t)(INT_MAX) + 1);

/* Structure for holding readability and writability events for a socket. */
struct socketrec {
	struct eventrec * reader;
	struct eventrec * writer;
	size_t pollpos;
};

/* List of sockets. */
ELASTICARRAY_DECL(SOCKETLIST, socketlist, struct socketrec);
static SOCKETLIST S = NULL;

/* Poll structures. */
static struct pollfd * fds;

/* Number of poll structures allocated in array. */
static size_t fds_alloc;

/* Number of poll structures initialized. */
static size_t nfds;

/* Position to which events_network_get has scanned in *fds. */
static size_t fdscanpos;

/**
 * Invariants:
 * 1. Initialized entries in S and fds point to each other:
 *     S[i].pollpos < nfds ==> fds[S[i].pollpos].fd == i
 *     j < nfds ==> S[fds[j].fd].pollpos == j
 * 2. Descriptors with events registered are in the right place:
 *     S[i].reader != NULL ==> S[i].pollpos < nfds
 *     S[i].writer != NULL ==> S[i].pollpos < nfds
 * 3. Descriptors without events registered aren't in the way:
 *     (S[i].reader == NULL && S[i].writer == NULL) ==> S[i].pollpos == -1
 * 4. Descriptors with events registered have the right masks:
 *     S[i].reader != NULL <==> (fds[S[i].pollpos].events & POLLIN) != 0
 *     S[i].writer != NULL <==> (fds[S[i].pollpos].events & POLLOUT) != 0
 * 5. We don't have events ready which we don't want:
 *     (fds[j].revents & (POLLIN | POLLOUT) & (~fds[j].events])) == 0
 * 6. Returned events are in position to be scanned later:
 *     fds[j].revents != 0 ==> f < fdscanpos.
 */

static void events_network_shutdown(void);

/* Initialize data structures if we haven't already done so. */
static int
init(void)
{

	/* If we're already initialized, do nothing. */
	if (S != NULL)
		goto done;

	/* Initialize the socket list. */
	if ((S = socketlist_init(0)) == NULL)
		goto err0;

	/* We have no poll structures allocated or initialized. */
	fds = NULL;
	fds_alloc = nfds = fdscanpos = 0;

	/* Clean up the socket list at exit. */
	if (atexit(events_network_shutdown))
		goto err0;

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
		socketlist_get(S, i)->pollpos = (size_t)(-1);
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Add a new descriptor to the pollfd array. */
static int
growpollfd(size_t fd)
{
	size_t new_fds_alloc;
	struct pollfd * new_fds;

	/* We should not be called if the descriptor is already listed. */
	assert(socketlist_get(S, fd)->pollpos == (size_t)(-1));

	/* Expand the pollfd allocation if needed. */
	if (fds_alloc == nfds) {
		new_fds_alloc = fds_alloc == 0 ? 16 : fds_alloc * 2;
		if (new_fds_alloc > SIZE_MAX / sizeof(struct pollfd)) {
			errno = ENOMEM;
			goto err0;
		}
		if ((new_fds = realloc(fds,
		    new_fds_alloc * sizeof(struct pollfd))) == NULL)
			goto err0;
		fds = new_fds;
		fds_alloc = new_fds_alloc;
	}

	/* Sanity-check. */
	assert(nfds < fds_alloc);
	assert(fd < INT_MAX);

	/* Initialize pollfd structure. */
	fds[nfds].fd = (int)fd;
	fds[nfds].events = 0;
	fds[nfds].revents = 0;

	/* Point at pollfd structure. */
	socketlist_get(S, fd)->pollpos = nfds;

	/* We now have one more pollfd structure. */
	nfds++;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Clear a bit from a pollfd and maintain invariants. */
static void
clearbit(size_t pollpos, short bit)
{

	/* Clear the bit. */
	fds[pollpos].events &= ~bit;
	fds[pollpos].revents &= ~bit;

	/* Is this pollfd in the way? */
	if (fds[pollpos].events == 0) {
		/* Clear the descriptor's pollpos pointer. */
		socketlist_get(S,
		    (size_t)fds[pollpos].fd)->pollpos = (size_t)(-1);

		/* If this wasn't the last pollfd, move another one up. */
		if (pollpos != nfds - 1) {
			memcpy(&fds[pollpos], &fds[nfds-1],
			    sizeof(struct pollfd));
			socketlist_get(S,
			    (size_t)fds[pollpos].fd)->pollpos = pollpos;
		}

		/* Shrink the pollfd array. */
		nfds--;
	}
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
	if (init())
		goto err0;

	/* Sanity-check socket number. */
	if (s < 0) {
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
	    (growsocketlist((size_t)s + 1) != 0))
		goto err0;

	/* Look up the relevant event pointer. */
	if (op == EVENTS_NETWORK_OP_READ)
		r = &socketlist_get(S, (size_t)s)->reader;
	else
		r = &socketlist_get(S, (size_t)s)->writer;

	/* Error out if we already have an event registered. */
	if (*r != NULL) {
		errno = EEXIST;
		goto err0;
	}

	/* Register the new event. */
	if ((*r = events_mkrec(func, cookie)) == NULL)
		goto err0;

	/* If we had no events registered, start a clock. */
	if (nfds == 0)
		events_network_selectstats_startclock();

	/* If this descriptor isn't in the pollfd array, add it. */
	if (socketlist_get(S, (size_t)s)->pollpos == (size_t)(-1)) {
		if (growpollfd((size_t)s))
			goto err1;
	}

	/* Set the appropriate event flag. */
	if (op == EVENTS_NETWORK_OP_READ)
		fds[socketlist_get(S, (size_t)s)->pollpos].events |= POLLIN;
	else
		fds[socketlist_get(S, (size_t)s)->pollpos].events |= POLLOUT;

	/* Success! */
	return (0);

err1:
	events_freerec(*r);
	*r = NULL;
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
	if (init())
		goto err0;

	/* Sanity-check socket number. */
	if (s < 0) {
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
		r = &socketlist_get(S, (size_t)s)->reader;
	else
		r = &socketlist_get(S, (size_t)s)->writer;

	/* Check if we have an event. */
	if (*r == NULL) {
		errno = ENOENT;
		goto err0;
	}

	/* Free the event. */
	events_freerec(*r);
	*r = NULL;

	/* Clear the appropriate pollfd bit(s). */
	if (op == EVENTS_NETWORK_OP_READ)
		clearbit(socketlist_get(S, (size_t)s)->pollpos, POLLIN);
	else
		clearbit(socketlist_get(S, (size_t)s)->pollpos, POLLOUT);

	/* If that was the last remaining event, stop the clock. */
	if (nfds == 0)
		events_network_selectstats_stopclock();

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_network_select(tv, interrupt_requested):
 * Check for socket readiness events, waiting up to ${tv} time if there are
 * no sockets immediately ready, or indefinitely if ${tv} is NULL.  The value
 * stored in ${tv} may be modified.  If ${*interrupt_requested} is non-zero
 * and a signal is received, exit.
 */
int
events_network_select(struct timeval * tv,
    volatile sig_atomic_t * interrupt_requested)
{
	int timeout;

	/* Initialize if necessary. */
	if (init())
		goto err0;

	/*
	 * Convert timeout to an integer number of ms.  We round up in order
	 * to avoid creating busy loops when 0 < ${tv} < 1 ms.
	 */
	if (tv == NULL)
		timeout = -1;
	else if (tv->tv_sec >= INT_MAX / 1000)
		timeout = INT_MAX;
	else
		timeout = (int)(tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000);

	/* We're about to call poll! */
	events_network_selectstats_select();

	/* Poll. */
	while (poll(fds, (nfds_t)nfds, timeout) == -1) {
		/* EINTR is harmless, unless we've requested an interrupt. */
		if (errno == EINTR) {
			if (*interrupt_requested)
				break;
			continue;
		}

		/* Anything else is an error. */
		warnp("poll()");
		goto err0;
	}

	/* If we have any events registered, start the clock again. */
	if (nfds > 0)
		events_network_selectstats_startclock();

	/* Start scanning at the last registered descriptor and work down. */
	fdscanpos = nfds - 1;

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

	/* We haven't found any events yet. */
	r = NULL;

	/* Scan through the pollfds looking for ready descriptors. */
	for (; fdscanpos < nfds; fdscanpos--) {
		/* Did we poll on an invalid descriptor? */
		assert((fds[fdscanpos].revents & POLLNVAL) == 0);

		/*
		 * If either POLLERR ("an exceptional condition") or POLLHUP
		 * ("has been disconnected") is set, then we should invoke
		 * whatever callbacks we have available.
		 */
		if (fds[fdscanpos].revents & (POLLERR | POLLHUP)) {
			fds[fdscanpos].revents &= ~(POLLERR | POLLHUP);
			fds[fdscanpos].revents |= fds[fdscanpos].events;
		}

		/* Are we ready for reading? */
		if (fds[fdscanpos].revents & POLLIN) {
			r = socketlist_get(S,
			    (size_t)fds[fdscanpos].fd)->reader;
			socketlist_get(S,
			    (size_t)fds[fdscanpos].fd)->reader = NULL;
			clearbit(fdscanpos, POLLIN);
			break;
		}

		/* Are we ready for reading? */
		if (fds[fdscanpos].revents & POLLOUT) {
			r = socketlist_get(S,
			    (size_t)fds[fdscanpos].fd)->writer;
			socketlist_get(S,
			    (size_t)fds[fdscanpos].fd)->writer = NULL;
			clearbit(fdscanpos, POLLOUT);
			break;
		}
	}

	/* If we're returning the last registered event, stop the clock. */
	if ((r != NULL) && (nfds == 0))
		events_network_selectstats_stopclock();

	/* Return the event we found, or NULL if we didn't find any. */
	return (r);
}

/**
 * events_network_shutdown(void):
 * Clean up and free memory.  This should run automatically via atexit.
 */
static void
events_network_shutdown(void)
{

	/* If we're not initialized, do nothing. */
	if (S == NULL)
		return;

	/* If we have any registered events, do nothing. */
	if (nfds > 0)
		return;

	/* Free the pollfd array. */
	free(fds);
	fds = NULL;
	fds_alloc = 0;

	/* Free the socket list. */
	socketlist_free(S);
	S = NULL;
}
