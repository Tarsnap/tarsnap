#include <sys/time.h>

#include <stdlib.h>
#include <string.h>

#include "ptrheap.h"

#include "timerqueue.h"

struct timerqueue {
	struct ptrheap * H;
};

struct timerrec {
	struct timeval tv;
	size_t rc;
	void * ptr;
};

/* Compare two timevals. */
static int
tvcmp(const struct timeval * x, const struct timeval * y)
{

	/* Does one have more seconds? */
	if (x->tv_sec > y->tv_sec)
		return (1);
	if (x->tv_sec < y->tv_sec)
		return (-1);

	/* Does one have more microseconds? */
	if (x->tv_usec > y->tv_usec)
		return (1);
	if (x->tv_usec < y->tv_usec)
		return (-1);

	/* They must be equal. */
	return (0);
}

/* Record-comparison callback from ptrheap. */
static int
compar(void * cookie, const void * x, const void * y)
{
	const struct timerrec * _x = x;
	const struct timerrec * _y = y;

	(void)cookie; /* UNUSED */

	/* Compare the times. */
	return (tvcmp(&_x->tv, &_y->tv));
}

/* Cookie-recording callback from ptrheap. */
static void
setreccookie(void * cookie, void * ptr, size_t rc)
{
	struct timerrec * rec = ptr;

	(void)cookie; /* UNUSED */

	rec->rc = rc;
}

/**
 * timerqueue_init(void):
 * Create and return an empty timer priority queue.
 */
struct timerqueue *
timerqueue_init(void)
{
	struct timerqueue * Q;

	/* Allocate structure. */
	if ((Q = malloc(sizeof(struct timerqueue))) == NULL)
		goto err0;

	/* Allocate heap. */
	if ((Q->H = ptrheap_init(compar, setreccookie, Q)) == NULL)
		goto err1;

	/* Success! */
	return (Q);

err1:
	free(Q);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * timerqueue_add(Q, tv, ptr):
 * Add the pair (${tv}, ${ptr}) to the priority queue ${Q}.  Return a cookie
 * which can be passed to timerqueue_delete() or timerqueue_increase().
 */
void *
timerqueue_add(struct timerqueue * Q, const struct timeval * tv, void * ptr)
{
	struct timerrec * r;

	/* Allocate (timeval, ptr) pair record. */
	if ((r = malloc(sizeof(struct timerrec))) == NULL)
		goto err0;

	/* Fill in values. */
	memcpy(&r->tv, tv, sizeof(struct timeval));
	r->ptr = ptr;

	/*
	 * Add the record to the heap.  The value r->rc will be filled in
	 * by setreccookie which will be called by ptrheap_add.
	 */
	if (ptrheap_add(Q->H, r))
		goto err1;

	/* Success! */
	return (r);

err1:
	free(r);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * timerqueue_delete(Q, cookie):
 * Delete the (timeval, ptr) pair associated with the cookie ${cookie} from
 * the priority queue ${Q}.
 */
void
timerqueue_delete(struct timerqueue * Q, void * cookie)
{
	struct timerrec * r = cookie;

	/* Remove the record from the heap. */
	ptrheap_delete(Q->H, r->rc);

	/* Free the record. */
	free(r);
}

/**
 * timerqueue_increase(Q, cookie, tv):
 * Increase the timer associated with the cookie ${cookie} in the priority
 * queue ${Q} to ${tv}.
 */
void
timerqueue_increase(struct timerqueue * Q, void * cookie,
    const struct timeval * tv)
{
	struct timerrec * r = cookie;

	/* Adjust timer value. */
	memcpy(&r->tv, tv, sizeof(struct timeval));

	/* Inform the heap that the record value has increased. */
	ptrheap_increase(Q->H, r->rc);
}

/**
 * timerqueue_getmin(Q):
 * Return a pointer to the least timeval in ${Q}, or NULL if the priority
 * queue is empty.  The pointer will remain valid until the next call to a
 * timerqueue_* function.  This function cannot fail.
 */
const struct timeval *
timerqueue_getmin(struct timerqueue * Q)
{
	struct timerrec * r;

	/* Get the minimum element from the heap. */
	r = ptrheap_getmin(Q->H);

	/* If we have an element, return its timeval; otherwise, NULL. */
	if (r != NULL)
		return (&r->tv);
	else
		return (NULL);
}

/**
 * timerqueue_getptr(Q, tv):
 * If the least timeval in ${Q} is less than or equal to ${tv}, return the
 * associated pointer and remove the pair from the priority queue.  If not,
 * return NULL.  This function cannot fail.
 */
void *
timerqueue_getptr(struct timerqueue * Q, const struct timeval * tv)
{
	struct timerrec * r;
	void * ptr;

	/*
	 * Get the minimum element from the heap.  Return NULL if the heap
	 * has no minimum element (i.e., is empty).
	 */
	if ((r = ptrheap_getmin(Q->H)) == NULL)
		return (NULL);

	/* If the minimum timeval is greater than ${tv}, return NULL. */
	if (tvcmp(&r->tv, tv) > 0)
		return (NULL);

	/* Remove this record from the heap. */
	ptrheap_deletemin(Q->H);

	/* Extract its pointer. */
	ptr = r->ptr;

	/* Free the record. */
	free(r);

	/*
	 * And finally return the pointer which was associated with the
	 * (formerly) minimum timeval in the heap.
	 */
	return (ptr);
}

/**
 * timerqueue_free(Q):
 * Free the timer priority queue ${Q}.
 */
void
timerqueue_free(struct timerqueue * Q)
{
	struct timerrec * r;

	/* Behave consistently with free(NULL). */
	if (Q == NULL)
		return;

	/* Extract elements from the heap and free them one by one. */
	while ((r = ptrheap_getmin(Q->H)) != NULL) {
		free(r);
		ptrheap_deletemin(Q->H);
	}

	/* Free the heap. */
	ptrheap_free(Q->H);

	/* Free the timer priority queue structure. */
	free(Q);
}
