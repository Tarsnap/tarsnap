#include <stdlib.h>
#include <string.h>

#include "elasticarray.h"
#include "events.h"
#include "warnp.h"

#include "tsnetwork.h"
#include "tsnetwork_internal.h"

/* Sleepers. */
struct sleeper {
	network_callback * callback;
	void * cookie;
	void * event_cookie;
};
ELASTICARRAY_DECL(SLEEPERS, sleepers, struct sleeper *);
static SLEEPERS sleepers = NULL;

/* Callback from network_sleep when timer expires. */
static int
callback_timer(void * cookie)
{
	struct sleeper * sp = cookie;

	/* This callback is no longer pending. */
	sp->event_cookie = NULL;

	/* Do callback. */
	return ((sp->callback)(sp->cookie, NETWORK_STATUS_TIMEOUT));
}

/**
 * network_sleep(timeo, callback, cookie):
 * Register a callback to be performed by network_select once the specified
 * timeout has expired.  Return a handle which can be passed to
 * network_desleep().
 */
int
network_sleep(struct timeval * timeo,
    network_callback * callback, void * cookie)
{
	struct sleeper s;
	struct sleeper * sp = NULL;	/* Silence bogus gcc warning. */
	size_t h;

	/* Initialize array if required. */
	if (sleepers == NULL) {
		if ((sleepers = sleepers_init(0)) == NULL)
			goto err0;
	}

	/* Construct sleeper record. */
	s.callback = callback;
	s.cookie = cookie;
	s.event_cookie = NULL;

	/* Search for empty space. */
	for (h = 0; h < sleepers_getsize(sleepers); h++) {
		sp = *sleepers_get(sleepers, h);
		if (sp->event_cookie == NULL) {
			/* Use this one. */
			memcpy(sp, &s, sizeof(struct sleeper));
			break;
		}
	}

	/* If we didn't find an empty space, add a new sleeper. */
	if (h == sleepers_getsize(sleepers)) {
		/* Don't have too many sleepers... */
		if (h == 1024) {
			warn0("Too many sleepers");
			goto err0;
		}

		/* Allocate a record. */
		if ((sp = malloc(sizeof(struct sleeper))) == NULL)
			goto err0;

		/* Copy data in. */
		memcpy(sp, &s, sizeof(struct sleeper));

		/* Append the record. */
		if (sleepers_append(sleepers, &sp, 1))
			goto err0;
	}

	/* Register the timer event. */
	if ((sp->event_cookie =
	    events_timer_register(callback_timer, sp, timeo)) == NULL)
		goto err0;

	/* Success! */
	return (h);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_desleep(handle):
 * Deregister the callback associated with the provided handle.  The
 * callback will be called with a status of NETWORK_STATUS_CANCEL.
 */
int
network_desleep(int handle)
{
	struct sleeper * sp;

	/* Sanity-check the handle. */
	if (handle >= (int)sleepers_getsize(sleepers)) {
		warn0("Invalid sleeper handle: %d", handle);
		goto err0;
	}

	/* Grab the relevant sleeper record. */
	sp = *sleepers_get(sleepers, handle);

	/* If there is no timer, return silently. */
	if (sp->event_cookie == NULL)
		return (0);

	/* Cancel the timer. */
	events_timer_cancel(sp->event_cookie);
	sp->event_cookie = NULL;

	/* Invoke the callback. */
	return ((sp->callback)(sp->cookie, NETWORK_STATUS_CANCEL));

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_sleep_fini(void):
 * Free resources allocated.
 */
void
network_sleep_fini(void)
{
	struct sleeper * sp;
	size_t i;

	/* Nothing to do if we're uninitialized. */
	if (sleepers == NULL)
		return;

	/* Free records. */
	for (i = 0; i < sleepers_getsize(sleepers); i++) {
		sp = *sleepers_get(sleepers, i);

		/* If this sleep is no longer in progress, free the record. */
		if (sp->event_cookie == NULL)
			free(sp);
	}

	/* Free the sleepers array. */
	sleepers_free(sleepers);
}
