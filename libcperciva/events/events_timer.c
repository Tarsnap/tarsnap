#include <sys/time.h>

#include <stdlib.h>
#include <string.h>

#include "monoclock.h"
#include "timerqueue.h"

#include "events.h"
#include "events_internal.h"

struct timerrec {
	struct eventrec * r;
	void * cookie;
	struct timeval tv_orig;
};

/* This also tracks whether we've initialized the atexit function. */
static struct timerqueue * Q = NULL;

static void events_timer_shutdown(void);

/* Set tv := <current time> + tdelta. */
static int
gettimeout(struct timeval * tv, struct timeval * tdelta)
{

	if (monoclock_get(tv))
		goto err0;
	tv->tv_sec += tdelta->tv_sec;
	if ((tv->tv_usec += tdelta->tv_usec) >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec += 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_timer_register(func, cookie, timeo):
 * Register ${func}(${cookie}) to be run ${timeo} in the future.  Return a
 * cookie which can be passed to events_timer_cancel or events_timer_reset.
 */
void *
events_timer_register(int (*func)(void *), void * cookie,
    const struct timeval * timeo)
{
	struct eventrec * r;
	struct timerrec * t;
	struct timeval tv;

	/* Create the timer queue if it doesn't exist yet. */
	if (Q == NULL) {
		if ((Q = timerqueue_init()) == NULL)
			goto err0;

		/* Clean up the timer queue at exit. */
		if (atexit(events_timer_shutdown))
			goto err0;
	}

	/* Bundle into an eventrec record. */
	if ((r = events_mkrec(func, cookie)) == NULL)
		goto err0;

	/* Create a timer record. */
	if ((t = malloc(sizeof(struct timerrec))) == NULL)
		goto err1;
	t->r = r;
	memcpy(&t->tv_orig, timeo, sizeof(struct timeval));

	/* Compute the absolute timeout. */
	if (gettimeout(&tv, &t->tv_orig))
		goto err2;

	/* Add this to the timer queue. */
	if ((t->cookie = timerqueue_add(Q, &tv, t)) == NULL)
		goto err2;

	/* Success! */
	return (t);

err2:
	free(t);
err1:
	events_freerec(r);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * events_timer_register_double(func, cookie, timeo):
 * As events_timer_register, but ${timeo} is a double-precision floating point
 * value specifying a number of seconds.
 */
void *
events_timer_register_double(int (*func)(void *), void * cookie,
    double timeo)
{
	struct timeval tv;

	/* Convert timeo to a struct timeval. */
	tv.tv_sec = (time_t)timeo;
	tv.tv_usec = (suseconds_t)((timeo - (double)tv.tv_sec) * 1000000.0);

	/* Schedule the timeout. */
	return (events_timer_register(func, cookie, &tv));
}

/**
 * events_timer_cancel(cookie):
 * Cancel the timer for which the cookie ${cookie} was returned by
 * events_timer_register.
 */
void
events_timer_cancel(void * cookie)
{
	struct timerrec * t = cookie;

	/* Remove from the timer queue. */
	timerqueue_delete(Q, t->cookie);

	/* Free the eventrec and timer records. */
	events_freerec(t->r);
	free(t);
}

/**
 * events_timer_reset(cookie):
 * Reset the timer for which the cookie ${cookie} was returned by
 * events_timer_register to its initial value.
 */
int
events_timer_reset(void * cookie)
{
	struct timerrec * t = cookie;
	struct timeval tv;

	/* Compute the new timeout. */
	if (gettimeout(&tv, &t->tv_orig))
		goto err0;

	/* Adjust the timer. */
	timerqueue_increase(Q, t->cookie, &tv);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_timer_min(timeo):
 * Return via ${timeo} a pointer to the minimum time which must be waited
 * before a timer will expire; or to NULL if there are no timers.  The caller
 * is responsible for freeing the returned pointer.
 */
int
events_timer_min(struct timeval ** timeo)
{
	struct timeval tnow;
	const struct timeval * tv;

	/* If we have no queue, we have no timers; return NULL. */
	if (Q == NULL) {
		*timeo = NULL;
		goto done;
	}

	/* Get the minimum timer from the queue. */
	tv = timerqueue_getmin(Q);

	/* If there are no timers, return NULL. */
	if (tv == NULL) {
		*timeo = NULL;
		goto done;
	}

	/* Allocate space for holding the returned timeval. */
	if ((*timeo = malloc(sizeof(struct timeval))) == NULL)
		goto err0;

	/* Get the current time... */
	if (monoclock_get(&tnow))
		goto err1;

	/* ... and compare it to the minimum timer. */
	if ((tnow.tv_sec > tv->tv_sec) ||
	    ((tnow.tv_sec == tv->tv_sec) && (tnow.tv_usec > tv->tv_usec))) {
		/* The timer has already expired, so return zero. */
		(*timeo)->tv_sec = 0;
		(*timeo)->tv_usec = 0;
	} else {
		/* Compute the difference. */
		(*timeo)->tv_sec = tv->tv_sec - tnow.tv_sec;
		(*timeo)->tv_usec = tv->tv_usec - tnow.tv_usec;
		if (tv->tv_usec < tnow.tv_usec) {
			(*timeo)->tv_usec += 1000000;
			(*timeo)->tv_sec -= 1;
		}
	}

done:
	/* Success! */
	return (0);

err1:
	free(*timeo);
err0:
	/* Failure! */
	return (-1);
}

/**
 * events_timer_get(r):
 * Return via ${r} a pointer to an eventrec structure corresponding to an
 * expired timer, and delete said timer; or to NULL if there are no expired
 * timers.  The caller is responsible for freeing the returned pointer.
 */
int
events_timer_get(struct eventrec ** r)
{
	struct timeval tnow;
	struct timerrec * t;

	/* If we have no queue, we have no timers; return NULL. */
	if (Q == NULL) {
		*r = NULL;
		goto done;
	}

	/* Get current time. */
	if (monoclock_get(&tnow))
		goto err0;

	/* Get an expired timer, if there is one. */
	t = timerqueue_getptr(Q, &tnow);

	/* If there is an expired timer... */
	if (t != NULL) {
		/* ... pass back the eventrec and free the timer. */
		*r = t->r;
		free(t);
	} else {
		/* Otherwise, return NULL. */
		*r = NULL;
	}

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * events_timer_shutdown(void):
 * Clean up and free memory.  This should run automatically via atexit.
 */
static void
events_timer_shutdown(void)
{

	/* If we have a queue and it is empty, free it. */
	if ((Q != NULL) && (timerqueue_getmin(Q) == NULL)) {
		timerqueue_free(Q);
		Q = NULL;
	}
}
