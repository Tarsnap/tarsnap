#include "bsdtar_platform.h"

#include <sys/select.h>
#include <sys/time.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "network_internal.h"
#include "warnp.h"

#include "network.h"

/*
 * Fake operation type to allow unconditional sleeping to reuse code from
 * the waiting-on-a-socket routines.  Note that while N.writers[k] and
 * N.readers[k] both refer to the socket with file descriptor k, N.waiters[k]
 * is completely unrelated (and has entries allocated sequentially as
 * calls to network_sleep are made).
 */
#define NETWORK_OP_WAIT	2

struct network_callback_internal {
	network_callback * callback;
	void * cookie;
	struct timeval timeout;
};

struct network_internal {
	struct network_callback_internal * writers;
	struct network_callback_internal * readers;
	struct network_callback_internal * waiters;
	size_t alloclen;
	int maxfd;
} N;

/* Bandwidth limits, in bytes per second. */
static double bwlimit_Bps_read;
static double bwlimit_Bps_write;

/* Current number of tokens in read/write bandwidth limit buckets. */
size_t network_bwlimit_read;
size_t network_bwlimit_write;

/* Last time tokens were added to read/write buckets. */
static struct timeval bwlimit_lastadd;

static int recalloc(void ** ptr, size_t clen, size_t nlen, size_t size);
static int docallback(struct network_callback_internal * C, int timedout);

/**
 * recalloc(ptr, clen, nlen, size):
 * Reallocate the pointer to which ${ptr} points so that it points to
 * enough space for ${nlen} objects of size ${size}.  Copy the first
 * min(${clen}, ${nlen}) such objects into the new buffer, and zero the
 * rest of the newly allocated buffer.
 */
static int
recalloc(void ** ptr, size_t clen, size_t nlen, size_t size)
{
	void * nbuf;
	size_t cplen;

	/* Make sure nlen * size won't overflow. */
	if (nlen > SIZE_MAX / size) {
		errno = ENOMEM;
		goto err0;
	}

	/* Perform new allocation. */
	if ((nbuf = malloc(nlen * size)) == NULL)
		goto err0;

	/* Copy data across. */
	if (clen > nlen)
		cplen = nlen;
	else
		cplen = clen;
	memcpy(nbuf, *ptr, cplen * size);

	/* Zero any uninitialized buffer contents. */
	memset((void *)((char *)nbuf + cplen * size), 0,
	    (nlen - cplen) * size);

	/* Free old buffer. */
	free(*ptr);

	/* Keep the pointer to the new buffer. */
	*ptr = nbuf;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * docallback(C, timedout):
 * Call (C->callback)(C->cookie, NETWORK_STATUS_OK), ..._TIMEOUT, or
 * ..._CANCEL depending on whether timedout is 0, 1, or -1 respectively; but
 * set C->callback to NULL first.
 */
static int
docallback(struct network_callback_internal * C, int timedout)
{
	network_callback * cb;
	int status = NETWORK_STATUS_OK;

	cb = C->callback;
	C->callback = NULL;
	if (timedout == -1)
		status = NETWORK_STATUS_CANCEL;
	else if (timedout == 1)
		status = NETWORK_STATUS_TIMEOUT;

	return ((cb)(C->cookie, status));
}

/**
 * network_init():
 * Initialize the network subsystem and return a cookie.
 */
int
network_init(void)
{

	/* No memory allocated yet. */
	N.writers = N.readers = N.waiters = NULL;
	N.alloclen = 0;

	/* No file descriptors yet, either. */
	N.maxfd = -1;

	/*
	 * Set a default bandwidth limit of 1 GBps until network_bwlimit is
	 * called to set a lower limit.  The value 1 GBps is chosen so that
	 * 2 seconds of bandwidth (see network_select) won't overflow size_t
	 * on a 32-bit system.
	 */
	bwlimit_Bps_read = bwlimit_Bps_write = 1000000000.;

	/* We have no tokens in our bandwidth quota buckets yet. */
	network_bwlimit_read = network_bwlimit_write = 0;

	/* The buckets were empty at the start of the epoch. */
	bwlimit_lastadd.tv_sec = 0;
	bwlimit_lastadd.tv_usec = 0;

	/* Success! */
	return (0);
}

/**
 * network_bwlimit(down, up):
 * Set the bandwidth rate limit to ${down} bytes per second of read bandwidth
 * and ${up} bytes per second of write bandwidth.  The values ${down} and
 * ${up} must be between 8000 and 10^9.
 */
void
network_bwlimit(double down, double up)
{

	bwlimit_Bps_read = down;
	bwlimit_Bps_write = up;
}

/**
 * network_register(fd, op, timeo, callback, cookie):
 * Register a callback to be performed by network_select when file descriptor
 * ${fd} is ready for operation ${op}, or once the timeout has expired.
 */
int
network_register(int fd, int op, struct timeval * timeo,
    network_callback * callback, void * cookie)
{
	struct network_callback_internal * nci;
	size_t newalloclen;

	/* Sanity-check the file descriptor. */
	if ((fd < 0) || (fd >= (int)FD_SETSIZE)) {
		warn0("Invalid file descriptor: %d", fd);
		goto err0;
	}

	/* Enlarge buffers if necessary. */
	if ((size_t)(fd) >= N.alloclen) {
		/*
		 * Guaranteed to enlarge the buffers enough AND to at least
		 * double the buffer size (thus preventing O(n^2) total
		 * reallocation costs).
		 */
		newalloclen = N.alloclen + fd + 1;
		if (newalloclen < N.alloclen) {
			/* Integer overflows are bad. */
			errno = ENOMEM;
			goto err0;
		}

		/*
		 * Enlarge the buffers and zero the new memory.  If one of
		 * the reallocations fails, we may end up with some buffers
		 * being allocated extra space; but this is harmless.
		 */
		if (recalloc((void **)&N.writers, N.alloclen, newalloclen,
		    sizeof(struct network_callback_internal)))
			goto err0;
		if (recalloc((void **)&N.readers, N.alloclen, newalloclen,
		    sizeof(struct network_callback_internal)))
			goto err0;
		if (recalloc((void **)&N.waiters, N.alloclen, newalloclen,
		    sizeof(struct network_callback_internal)))
			goto err0;

		/* The buffers have been enlarged. */
		N.alloclen = newalloclen;
	}

	/* Figure out which network_callback_internal we're touching. */
	switch (op) {
	case NETWORK_OP_READ:
		nci = &N.readers[fd];
		break;
	case NETWORK_OP_WRITE:
		nci = &N.writers[fd];
		break;
	case NETWORK_OP_WAIT:
		nci = &N.waiters[fd];
		break;
	default:
		warn0("Programmer error: "
		    "Invalid operation type in network_register");
		goto err0;
	}

	/* Make sure that we're not replacing an existing callback. */
	if (nci->callback != NULL) {
		warn0("Replacing callback: op = %d, fd = %d", op, fd);
		goto err0;
	}

	/* Set the callback and cookie. */
	nci->callback = callback;
	nci->cookie = cookie;

	/* Convert the timeout into an absolute time. */
	if (gettimeofday(&nci->timeout, NULL)) {
		warnp("gettimeofday()");
		nci->callback = NULL;
		goto err0;
	}
	if (timeo != NULL)
		tv_add(&nci->timeout, timeo);

	/* Update the number of the highest used fd if necessary. */
	if (N.maxfd < fd)
		N.maxfd = fd;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_deregister(fd, op):
 * Deregister the callback, if any, for operation ${op} on descriptor ${fd}.
 * The callback will be called with a status of NETWORK_STATUS_CANCEL.
 */
int
network_deregister(int fd, int op)
{
	struct network_callback_internal * nci;
	int rc = 0;

	/*
	 * Do nothing if fd is larger than the allocated array length.  This
	 * can occur if a socket is opened and closed without ever having a
	 * callback registered, e.g., if connect() fails without blocking.
	 */
	if ((size_t)(fd) >= N.alloclen)
		goto done;

	/* Figure out which network_callback_internal we're touching. */
	switch (op) {
	case NETWORK_OP_READ:
		nci = &N.readers[fd];
		break;
	case NETWORK_OP_WRITE:
		nci = &N.writers[fd];
		break;
	case NETWORK_OP_WAIT:
		nci = &N.waiters[fd];
		break;
	default:
		warn0("Programmer error: "
		    "Invalid operation type in network_register");
		goto err0;
	}

	/* Do the callback, if one exists. */
	if (nci->callback != NULL)
		rc = docallback(nci, -1);
	else
		rc = 0;

done:
	/* Return value from callback. */
	return (rc);

err0:
	/* Failure! */
	return (-1);
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
	int fd;

	/* Pick a "file descriptor" for which there is no waiter yet. */
	for (fd = 0; (size_t)(fd) < N.alloclen; fd++) {
		if (N.waiters[fd].callback == NULL)
			break;
	}

	/* Register the callback. */
	if (network_register(fd, NETWORK_OP_WAIT, timeo, callback, cookie))
		goto err0;

	/* Return the "file descriptor" as handle. */
	return (fd);

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

	/* Let network_deregister do the work. */
	return (network_deregister(handle, NETWORK_OP_WAIT));
}

/**
 * network_select(blocking):
 * Call select(2) on file descriptors provided via network_register and make
 * callbacks as appropriate (including timeouts).  Callbacks are deregistered
 * before being performed.  If ${blocking} is non-zero, allow select(2) to
 * block waiting for descriptors and timeouts.  Stop performing callbacks and
 * return the first non-zero value returned by a callback if one occurs.
 */
int
network_select(int blocking)
{
	fd_set readfds;
	fd_set writefds;
	struct timeval timeout;
	struct timeval curtime;
	int nready;
	int fd;
	int rc;
	int ntimeouts = 0;
	double tokensecs;

	/* Get current time. */
	if (gettimeofday(&curtime, NULL)) {
		warnp("gettimeofday()");
		goto err0;
	}

	/*
	 * Figure out how long it has been since we last added tokens to the
	 * bandwidth limit buckets.
	 */
	tokensecs = (curtime.tv_sec - bwlimit_lastadd.tv_sec) +
	    (curtime.tv_usec - bwlimit_lastadd.tv_usec) * 0.000001;
	memcpy(&bwlimit_lastadd, &curtime, sizeof(struct timeval));

	/*
	 * Add tokens to the read bandwidth token bucket, overflowing if we
	 * hit 2 seconds of bandwidth.  (Why 2 seconds?  Because it's more
	 * than a network RTT, so as long as we're called at least once every
	 * 2 seconds, we won't starve the TCP stack; and because it's small
	 * enough that we won't end up with incredibly bursty network traffic.
	 */
	if (network_bwlimit_read / bwlimit_Bps_read + tokensecs > 2)
		network_bwlimit_read = bwlimit_Bps_read * 2;
	else
		network_bwlimit_read += bwlimit_Bps_read * tokensecs;

	/* Do likewise for write bandwidth. */
	if (network_bwlimit_write / bwlimit_Bps_write + tokensecs > 2)
		network_bwlimit_write = bwlimit_Bps_write * 2;
	else
		network_bwlimit_write += bwlimit_Bps_write * tokensecs;

	/* If we're allowed to block, figure out how long to block for. */
	if (blocking) {
		/* Find the earliest timeout time, if prior to now + 1d. */
		memcpy(&timeout, &curtime, sizeof(struct timeval));
		timeout.tv_sec += 86400;
		for (fd = 0; fd <= N.maxfd; fd++) {
			if (N.readers[fd].callback != NULL) {
				ntimeouts++;
				if (tv_lt(&N.readers[fd].timeout, &timeout))
					memcpy(&timeout,
					    &N.readers[fd].timeout,
					    sizeof(struct timeval));
			}
			if (N.writers[fd].callback != NULL) {
				ntimeouts++;
				if (tv_lt(&N.writers[fd].timeout, &timeout))
					memcpy(&timeout,
					    &N.writers[fd].timeout,
					    sizeof(struct timeval));
			}
			if (N.waiters[fd].callback != NULL) {
				ntimeouts++;
				if (tv_lt(&N.waiters[fd].timeout, &timeout))
					memcpy(&timeout,
					    &N.waiters[fd].timeout,
					    sizeof(struct timeval));
			}
		}

		/* Subtract the current time, clamping at zero. */
		if (tv_lt(&timeout, &curtime))
			memset(&timeout, 0, sizeof(struct timeval));
		else
			tv_sub(&timeout, &curtime);

		/* Make sure that we have something to wait for. */
		if (ntimeouts == 0) {
			warn0("Blocking network_select with no callbacks!");
			goto err0;
		}
	} else {
		memset(&timeout, 0, sizeof(struct timeval));
	}

selectagain:
	/* Construct fd sets. */
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	for (fd = 0; fd <= N.maxfd; fd++) {
		if (N.readers[fd].callback != NULL)
			FD_SET(fd, &readfds);
		if (N.writers[fd].callback != NULL)
			FD_SET(fd, &writefds);
	}

	/*
	 * If we have no read bandwidth quota, zero the read set and set the
	 * blocking duration to a maximum of 1 ms.
	 */
	if (network_bwlimit_read == 0) {
		FD_ZERO(&readfds);
		if ((timeout.tv_sec > 0) || (timeout.tv_usec > 1000)) {
			timeout.tv_sec = 0;
			timeout.tv_usec = 1000;
		}
	}

	/* Do likewise for writes. */
	if (network_bwlimit_write == 0) {
		FD_ZERO(&writefds);
		if ((timeout.tv_sec > 0) || (timeout.tv_usec > 1000)) {
			timeout.tv_sec = 0;
			timeout.tv_usec = 1000;
		}
	}

	/* Call select(2). */
	while ((nready = select(N.maxfd + 1, &readfds, &writefds,
	    NULL, &timeout)) == -1) {
		/* EINTR is harmless. */
		if (errno == EINTR)
			continue;

		/* Anything else is an error. */
		warnp("select()");
		goto err0;
	}

	/* Get current time so that we can detect timeouts. */
	if (gettimeofday(&curtime, NULL)) {
		warnp("gettimeofday()");
		goto err0;
	}

	/*
	 * Scan the descriptors in decreasing order, just in case maxfd is
	 * changed as a result of a callback.
	 */
	for (fd = N.maxfd; fd >= 0; fd--) {
		/* Is the descriptor ready for reading? */
		if ((N.readers[fd].callback != NULL) &&
		    FD_ISSET(fd, &readfds))
			if ((rc = docallback(&N.readers[fd], 0)) != 0)
				goto cberr;
		/* Did the read callback timeout? */
		if ((N.readers[fd].callback != NULL) &&
		    tv_lt(&N.readers[fd].timeout, &curtime))
			if ((rc = docallback(&N.readers[fd], 1)) != 0)
				goto cberr;

		/* Is the descriptor ready for writing? */
		if ((N.writers[fd].callback != NULL) &&
		    FD_ISSET(fd, &writefds))
			if ((rc = docallback(&N.writers[fd], 0)) != 0)
				goto cberr;
		/* Did the write callback timeout? */
		if ((N.writers[fd].callback != NULL) &&
		    tv_lt(&N.writers[fd].timeout, &curtime))
			if ((rc = docallback(&N.writers[fd], 1)) != 0)
				goto cberr;

		/* Did the wait callback timeout? */
		if ((N.waiters[fd].callback != NULL) &&
		    tv_lt(&N.waiters[fd].timeout, &curtime))
			if ((rc = docallback(&N.waiters[fd], 1)) != 0)
				goto cberr;

		/* Can we reduce maxfd? */
		if ((fd == N.maxfd) &&
		    (N.readers[fd].callback == NULL) &&
		    (N.writers[fd].callback == NULL) &&
		    (N.waiters[fd].callback == NULL))
			N.maxfd--;
	}

	/*
	 * If we did at least one callback and we're not blocking, go back
	 * and see if we can do any more callbacks.
	 */
	if ((nready > 0) && (blocking == 0))
		goto selectagain;

	/* Success! */
	return (0);

cberr:
	/* A callback returned a non-zero value. */
	return (rc);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_spin(done):
 * Repeatedly call network_select until either an error occurs or the value
 * pointed to by ${done} is non-zero.
 */
int
network_spin(int * done)
{

	while (*done == 0) {
		if (network_select(1))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_fini():
 * Free resources associated with the network subsystem.
 */
void
network_fini(void)
{

	/* Free callbacks, cookies, and timeouts. */
	free(N.readers);
	free(N.writers);
	free(N.waiters);
}
