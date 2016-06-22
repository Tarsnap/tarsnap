#ifndef _TIMERQUEUE_H_
#define _TIMERQUEUE_H_

#include <sys/time.h>

/* Timer priority queue.  Contains (timeval, ptr) pairs. */

/* Opaque timer priority queue type. */
struct timerqueue;

/**
 * timerqueue_init(void):
 * Create and return an empty timer priority queue.
 */
struct timerqueue * timerqueue_init(void);

/**
 * timerqueue_add(Q, tv, ptr):
 * Add the pair (${tv}, ${ptr}) to the priority queue ${Q}.  Return a cookie
 * which can be passed to timerqueue_delete or timerqueue_increase.
 */
void * timerqueue_add(struct timerqueue *, const struct timeval *, void *);

/**
 * timerqueue_delete(Q, cookie):
 * Delete the (timeval, ptr) pair associated with the cookie ${cookie} from
 * the priority queue ${Q}.
 */
void timerqueue_delete(struct timerqueue *, void *);

/**
 * timerqueue_increase(Q, cookie, tv):
 * Increase the timer associated with the cookie ${cookie} in the priority
 * queue ${Q} to ${tv}.
 */
void timerqueue_increase(struct timerqueue *, void *, const struct timeval *);

/**
 * timerqueue_getmin(Q):
 * Return a pointer to the least timeval in ${Q}, or NULL if the priority
 * queue is empty.  The pointer will remain valid until the next call to a
 * timerqueue_* function.  This function cannot fail.
 */
const struct timeval * timerqueue_getmin(struct timerqueue *);

/**
 * timerqueue_getptr(Q, tv):
 * If the least timeval in ${Q} is less than or equal to ${tv}, return the
 * associated pointer and remove the pair from the priority queue.  If not,
 * return NULL.  This function cannot fail.
 */
void * timerqueue_getptr(struct timerqueue *, const struct timeval *);

/**
 * timerqueue_free(Q):
 * Free the timer priority queue ${Q}.
 */
void timerqueue_free(struct timerqueue *);

#endif /* !_TIMERQUEUE_H_ */
