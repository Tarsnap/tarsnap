#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "mpool.h"

#include "events_internal.h"
#include "events.h"

struct eventq {
	struct eventrec * r;
	struct eventq * next;
	struct eventq * prev;
	int prio;
};

MPOOL(eventq, struct eventq, 4096);

/* First nodes in the linked lists. */
static struct eventq * heads[32] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* For non-NULL heads[i], tails[i] is the last node in the list. */
static struct eventq * tails[32];

/* For i < minq, heads[i] == NULL. */
static int minq = 0;

/**
 * events_immediate_register(func, cookie, prio):
 * Register ${func}(${cookie}) to be run the next time events_run is invoked,
 * after immediate events with smaller ${prio} values and before events with
 * larger ${prio} values.  The value ${prio} must be in the range [0, 31].
 * Return a cookie which can be passed to events_immediate_cancel.
 */
void *
events_immediate_register(int (*func)(void *), void * cookie, int prio)
{
	struct eventrec * r;
	struct eventq * q;

	/* Sanity check. */
	assert((prio >= 0) && (prio < 32));

	/* Bundle into an eventrec record. */
	if ((r = events_mkrec(func, cookie)) == NULL)
		goto err0;

	/* Create a linked list node. */
	if ((q = mpool_eventq_malloc()) == NULL)
		goto err1;
	q->r = r;
	q->next = NULL;
	q->prev = NULL;
	q->prio = prio;

	/* Add to the queue. */
	if (heads[prio] == NULL) {
		heads[prio] = q;
		if (prio < minq)
			minq = prio;
	} else {
		tails[prio]->next = q;
		q->prev = tails[prio];
	}
	tails[prio] = q;

	/* Success! */
	return (q);

err1:
	events_freerec(r);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * events_immediate_cancel(cookie):
 * Cancel the immediate event for which the cookie ${cookie} was returned by
 * events_immediate_register.
 */
void
events_immediate_cancel(void * cookie)
{
	struct eventq * q = cookie;
	int prio = q->prio;

	/* If we have a predecessor, point it at our successor. */
	if (q->prev != NULL)
		q->prev->next = q->next;
	else
		heads[prio] = q->next;

	/* If we have a successor, point it at our predecessor. */
	if (q->next != NULL)
		q->next->prev = q->prev;
	else
		tails[prio] = q->prev;

	/* Free the eventrec. */
	events_freerec(q->r);

	/* Return the node to the malloc pool. */
	mpool_eventq_free(q);
}

/**
 * events_immediate_get(void):
 * Remove and return an eventrec structure from the immediate event queue,
 * or return NULL if there are no such events.  The caller is responsible for
 * freeing the returned memory.
 */
struct eventrec *
events_immediate_get(void)
{
	struct eventq * q;
	struct eventrec * r;
	int prio;

	/* Scan through priorities until we find an event or run out. */
	for (prio = minq; prio < 32; prio++) {
		/* Did we find an event? */
		if (heads[prio] != NULL)
			break;

		/* This queue is empty; move on to the next one. */
		minq++;
	}

	/* Are there any events? */
	if (prio == 32)
		return (NULL);

	/* Remove the first node from the linked list. */
	q = heads[prio];
	heads[prio] = q->next;
	if (heads[prio] != NULL)
		heads[prio]->prev = NULL;

	/* Extract the eventrec. */
	r = q->r;

	/* Return the node to the malloc pool. */
	mpool_eventq_free(q);

	/* Return the eventrec. */
	return (r);
}
