#include "bsdtar_platform.h"

#include <sys/time.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tsnetwork_internal.h"
#include "tvmath.h"

#include "tsnetwork.h"

struct network_writeq_buf {
	const uint8_t * buf;
	size_t buflen;
	struct timeval timeo;
	int abstimeo;
	network_callback * callback;
	void * cookie;
	struct network_writeq_buf * next;
};
struct network_writeq_internal {
	int fd;
	struct network_writeq_buf * head;
	struct network_writeq_buf ** tailptr;
};

static int dowrite(struct network_writeq_internal *);
static network_callback callback_bufdone;

static int
dowrite(struct network_writeq_internal * Q)
{
	struct network_writeq_buf * QB = Q->head;
	struct timeval timeo;

	/* Sanity check that the queue is non-empty */
	assert(Q->head != NULL);

	/* Figure out how long to allow for this buffer write. */
	memcpy(&timeo, &QB->timeo, sizeof(struct timeval));
	if (QB->abstimeo && tvmath_subctime(&timeo))
		goto err0;

	/* Write the buffer. */
	if (tsnetwork_write(Q->fd, QB->buf, QB->buflen, &timeo, &timeo,
	    callback_bufdone, Q))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * callback_bufdone(cookie, status):
 * Call the upstream callback for the buffer at the head of the write queue
 * ${cookie}, remove it from the queue, and write the next buffer.
 */
static int
callback_bufdone(void * cookie, int status)
{
	struct network_writeq_internal * Q = cookie;
	struct network_writeq_buf * head_old;
	int rc;

	/* Unlink the current buffer from the queue. */
	head_old = Q->head;
	Q->head = head_old->next;

	/* Update tail pointer if necessary. */
	if (Q->tailptr == &head_old->next)
		Q->tailptr = &Q->head;

	/*
	 * A callback of NETWORK_STATUS_CLOSED in response to an attempt to
	 * write zero bytes is really a NETWORK_STATUS_ZEROBYTE.
	 */
	if ((status == NETWORK_STATUS_CLOSED) && (head_old->buflen == 0))
		status = NETWORK_STATUS_ZEROBYTE;

	/*
	 * If there's another buffer waiting to be written, register it to
	 * be sent.  If not and we're not handling an error, uncork the
	 * socket.
	 */
	if (Q->head != NULL) {
		if (dowrite(Q))
			goto err1;
	} else {
		if ((status == NETWORK_STATUS_OK) && network_uncork(Q->fd))
			status = NETWORK_STATUS_ERR;
	}

	/* Call the upstream callback. */
	rc = (head_old->callback)(head_old->cookie, status);

	/* Free the write parameters structure. */
	free(head_old);

	/* Return value from callback. */
	return (rc);

err1:
	(head_old->callback)(head_old->cookie, status);
	free(head_old);

	/* Failure! */
	return (-1);
}

/**
 * network_writeq_init(fd):
 * Construct a queue to be used for writing data to ${fd}.
 */
NETWORK_WRITEQ *
network_writeq_init(int fd)
{
	struct network_writeq_internal * Q;

	/* Allocate memory. */
	if ((Q = malloc(sizeof(struct network_writeq_internal))) == NULL)
		goto err0;

	/* Initialize structure. */
	Q->fd = fd;
	Q->head = NULL;
	Q->tailptr = &Q->head;

	/* Success! */
	return (Q);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * _network_writeq_add(Q, buf, buflen, timeo, callback, cookie, abstimeo):
 * Add a buffer write to the specified write queue.  The callback function
 * will be called when the write is finished, fails, or is cancelled.
 * If ${abstimeo} is zero, the timeout is relative to when the buffer in
 * question starts to be written (i.e., when the previous buffered write
 * finishes); otherwise, the timeout is relative to the present time.  If
 * ${buflen} is zero, the callback will be performed, at the appropriate
 * point, with a status of NETWORK_STATUS_ZEROBYTE.
 */
int
_network_writeq_add(NETWORK_WRITEQ * Q, const uint8_t * buf, size_t buflen,
    struct timeval * timeo, network_callback * callback, void * cookie,
    int abstimeo)
{
	struct network_writeq_buf * QB;
	struct network_writeq_buf ** tailptr_old;
	struct network_writeq_buf * head_old;

	/* Wrap parameters into a structure. */
	if ((QB = malloc(sizeof(struct network_writeq_buf))) == NULL)
		goto err0;
	QB->buf = buf;
	QB->buflen = buflen;
	memcpy(&QB->timeo, timeo, sizeof(struct timeval));
	QB->abstimeo = abstimeo;
	QB->callback = callback;
	QB->cookie = cookie;
	QB->next = NULL;

	/* Compute absolute time if appropriate. */
	if (abstimeo && tvmath_addctime(&QB->timeo))
		goto err1;

	/* Add this to the write queue. */
	head_old = Q->head;
	tailptr_old = Q->tailptr;
	*Q->tailptr = QB;
	Q->tailptr = &QB->next;

	/* If the queue head was NULL, we need to kick off the writing. */
	if (head_old == NULL) {
		/* Cork the socket so that we don't send small packets. */
		if (network_cork(Q->fd))
			goto err2;

		if (dowrite(Q))
			goto err2;
	}

	/* Success! */
	return (0);

err2:
	Q->tailptr = tailptr_old;
	*Q->tailptr = NULL;
err1:
	free(QB);
err0:
	/* Failure! */
	return (-1);
}

/**
 * network_writeq_cancel(Q):
 * Cancel all queued writes, including any partially completed writes.  Note
 * that since this leaves the connection in an indeterminate state (there is
 * no way to know how much data from the currently in-progress write was
 * written) this should probably only be used prior to closing a connection.
 * The callbacks for each pending write will be called with a status of
 * NETWORK_STATUS_DEQUEUE, and network_writeq_cancel will return the first
 * non-zero value returned by a callback.
 */
int
network_writeq_cancel(NETWORK_WRITEQ * Q)
{
	int rc = 0, rc2;

	/* Keep on deregistering callbacks until the queue is empty. */
	while (Q->head != NULL) {
		rc2 = network_deregister(Q->fd, NETWORK_OP_WRITE);
		rc = rc ? rc : rc2;
	}

	/* Return first non-zero result from deregistration. */
	return (rc);
}

/**
 * network_writeq_free(Q):
 * Free the specified write queue.  If there might be any pending writes,
 * network_writeq_cancel should be called first.
 */
void
network_writeq_free(NETWORK_WRITEQ * Q)
{
	struct network_writeq_buf * head_old;

	/* Behave consistently with free(NULL). */
	if (Q == NULL)
		return;

	/* Repeat until the queue is empty. */
	while (Q->head != NULL) {
		/* Unlink the current buffer from the queue. */
		head_old = Q->head;
		Q->head = Q->head->next;

		/* Free the write parameters structure. */
		free(head_old);
	}

	/* Free the queue structure itself. */
	free(Q);
}
