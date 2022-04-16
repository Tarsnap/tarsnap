#include <assert.h>
#include <stdlib.h>

#include "mpool.h"
#include "queue.h"

#include "events.h"
#include "events_internal.h"

struct eventq {
	struct eventrec * r;
	TAILQ_ENTRY(eventq) entries;
	int prio;
};

MPOOL(eventq, struct eventq, 4096);

/* First nodes in the linked lists. */
static TAILQ_HEAD(tailhead, eventq) heads[32] = {
	TAILQ_HEAD_INITIALIZER(heads[0]),
	TAILQ_HEAD_INITIALIZER(heads[1]),
	TAILQ_HEAD_INITIALIZER(heads[2]),
	TAILQ_HEAD_INITIALIZER(heads[3]),
	TAILQ_HEAD_INITIALIZER(heads[4]),
	TAILQ_HEAD_INITIALIZER(heads[5]),
	TAILQ_HEAD_INITIALIZER(heads[6]),
	TAILQ_HEAD_INITIALIZER(heads[7]),
	TAILQ_HEAD_INITIALIZER(heads[8]),
	TAILQ_HEAD_INITIALIZER(heads[9]),
	TAILQ_HEAD_INITIALIZER(heads[10]),
	TAILQ_HEAD_INITIALIZER(heads[11]),
	TAILQ_HEAD_INITIALIZER(heads[12]),
	TAILQ_HEAD_INITIALIZER(heads[13]),
	TAILQ_HEAD_INITIALIZER(heads[14]),
	TAILQ_HEAD_INITIALIZER(heads[15]),
	TAILQ_HEAD_INITIALIZER(heads[16]),
	TAILQ_HEAD_INITIALIZER(heads[17]),
	TAILQ_HEAD_INITIALIZER(heads[18]),
	TAILQ_HEAD_INITIALIZER(heads[19]),
	TAILQ_HEAD_INITIALIZER(heads[20]),
	TAILQ_HEAD_INITIALIZER(heads[21]),
	TAILQ_HEAD_INITIALIZER(heads[22]),
	TAILQ_HEAD_INITIALIZER(heads[23]),
	TAILQ_HEAD_INITIALIZER(heads[24]),
	TAILQ_HEAD_INITIALIZER(heads[25]),
	TAILQ_HEAD_INITIALIZER(heads[26]),
	TAILQ_HEAD_INITIALIZER(heads[27]),
	TAILQ_HEAD_INITIALIZER(heads[28]),
	TAILQ_HEAD_INITIALIZER(heads[29]),
	TAILQ_HEAD_INITIALIZER(heads[30]),
	TAILQ_HEAD_INITIALIZER(heads[31])
};

/* For i < minq, heads[i] == NULL. */
static int minq = 32;

/**
 * events_immediate_register(func, cookie, prio):
 * Register ${func}(${cookie}) to be run the next time events_run() is
 * invoked, after immediate events with smaller ${prio} values and before
 * events with larger ${prio} values.  The value ${prio} must be in the range
 * [0, 31].  Return a cookie which can be passed to events_immediate_cancel().
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
	q->prio = prio;

	/* Add to the queue. */
	TAILQ_INSERT_TAIL(&heads[prio], q, entries);

	/* Update minq if necessary. */
	if (prio < minq)
		minq = prio;

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
 * events_immediate_register().
 */
void
events_immediate_cancel(void * cookie)
{
	struct eventq * q = cookie;
	int prio = q->prio;

	/* Remove it from the list. */
	TAILQ_REMOVE(&heads[prio], q, entries);

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

	/* Advance past priorities which have no events. */
	while ((minq < 32) && (TAILQ_EMPTY(&heads[minq])))
		minq++;

	/* Are there any events? */
	if (minq == 32)
		return (NULL);

	/*
	 * Remove the first node from the highest priority non-empty linked
	 * list.
	 */
	q = TAILQ_FIRST(&heads[minq]);
	TAILQ_REMOVE(&heads[minq], q, entries);

	/* Extract the eventrec. */
	r = q->r;

	/* Return the node to the malloc pool. */
	mpool_eventq_free(q);

	/* Return the eventrec. */
	return (r);
}
