#include <sys/time.h>

#include <stdlib.h>

#include "mpool.h"

#include "events_internal.h"
#include "events.h"

/* Event structure. */
struct eventrec {
	int (*func)(void *);
	void * cookie;
};

MPOOL(eventrec, struct eventrec, 4096);

/* Zero timeval, for use with non-blocking event runs. */
struct timeval tv_zero = {0, 0};

/**
 * events_mkrec(func, cookie):
 * Package ${func}, ${cookie} into a struct eventrec.
 */
struct eventrec *
events_mkrec(int (*func)(void *), void * cookie)
{
	struct eventrec * r;

	/* Allocate structure. */
	if ((r = mpool_eventrec_malloc()) == NULL)
		goto err0;

	/* Initialize. */
	r->func = func;
	r->cookie = cookie;

	/* Success! */
	return (r);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * events_freerec(r):
 * Free the eventrec ${r}.
 */
void
events_freerec(struct eventrec * r)
{

	mpool_eventrec_free(r);
}

/* Do an event.  This makes events_run cleaner. */
static inline int
doevent(struct eventrec * r)
{
	int rc;

	/* Invoke the callback. */
	rc = (r->func)(r->cookie);

	/* Free the event record. */
	mpool_eventrec_free(r);

	/* Return the status code from the callback. */
	return (rc);
}

/**
 * events_run(void):
 * Run events.  Events registered via events_immediate_register will be run
 * first, in order of increasing ${prio} values; then events associated with
 * ready sockets registered via events_network_register; finally, events
 * associated with expired timers registered via events_timer_register will
 * be run.  If any event function returns a non-zero result, no further
 * events will be run and said non-zero result will be returned; on error,
 * -1 will be returned.
 */
int
events_run(void)
{
	struct eventrec * r;
	struct timeval * tv;
	int rc = 0;

	/* If we have any immediate events, process them and return. */
	if ((r = events_immediate_get()) != NULL) {
		while (r != NULL) {
			/* Process the event. */
			if ((rc = doevent(r)) != 0)
				goto done;

			/* Get the next event. */
			r = events_immediate_get();
		}

		/* We've processed at least one event; time to return. */
		goto done;
	}

	/*
	 * Figure out the maximum duration to block, and wait up to that
	 * duration for network events to become available.
	 */
	if (events_timer_min(&tv))
		goto err0;
	if (events_network_select(tv))
		goto err1;
	free(tv);

	/*
	 * Check for available immediate events, network events, and timer
	 * events, in that order of priority; exit only when no more events
	 * are available.
	 */
	do {
		/* Run an immediate event, if one is available. */
		if ((r = events_immediate_get()) != NULL) {
			if ((rc = doevent(r)) != 0)
				goto done;
			continue;
		}

		/* Run a network event, if one is available. */
		if ((r = events_network_get()) != NULL) {
			if ((rc = doevent(r)) != 0)
				goto done;
			continue;
		}

		/* Check if any new network events are available. */
		if (events_network_select(&tv_zero))
			goto err0;
		if ((r = events_network_get()) != NULL) {
			if ((rc = doevent(r)) != 0)
				goto done;
			continue;
		}

		/* Run a timer event, if one is available. */
		if (events_timer_get(&r))
			goto err0;
		if (r != NULL) {
			if ((rc = doevent(r)) != 0)
				goto done;
			continue;
		}

		/* No events available. */
		break;
	} while (1);

done:
	/* Success! */
	return (rc);

err1:
	free(tv);
err0:
	/* Failure! */
	return (-1);
}

/**
 * events_spin(done):
 * Run events until ${done} is non-zero (and return 0), an error occurs (and
 * return -1), or a callback returns a non-zero status (and return the status
 * code from the callback).
 */
int
events_spin(int * done)
{
	int rc = 0;

	/* Loop until we're done or have a non-zero status. */
	while ((done[0] == 0) && (rc == 0)) {
		/* Run events. */
		rc = events_run();
	}

	/* Return status code. */
	return (rc);
}

/**
 * events_shutdown(void):
 * Clean up and free memory.  This call is not necessary on program exit and
 * is only expected to be useful when checking for memory leaks.
 */
void
events_shutdown(void)
{

	events_network_shutdown();
	events_timer_shutdown();
}
