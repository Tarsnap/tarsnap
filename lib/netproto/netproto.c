#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "crypto.h"
#include "tsnetwork.h"
#include "warnp.h"

#include "netproto.h"
#include "netproto_internal.h"

static network_callback callback_sleep;

/**
 * netproto_printerr_internal(status):
 * Print the error message associated with the given status code.
 */
void
netproto_printerr_internal(int status)
{

	switch (status) {
	case NETWORK_STATUS_CONNERR:
		/* Could not connect. */
		warn0("Error connecting to server");
		break;
	case NETWORK_STATUS_ERR:
		/* Error is specified in errno. */
		warnp("Network error");
		break;
	case NETWORK_STATUS_NODATA:
	case NETWORK_STATUS_TIMEOUT:
		/* Server timed out. */
		warn0("Timeout communicating with server");
		break;
	case NETWORK_STATUS_CTIMEOUT:
		/* Server timed out. */
		warn0("Timeout connecting to server");
		break;
	case NETWORK_STATUS_CLOSED:
		/* Server closed connection. */
		warn0("Connection closed by server");
		break;
	case NETWORK_STATUS_CANCEL:
		/* Operation cancelled; no error message. */
		break;
	case NETPROTO_STATUS_PROTERR:
		/* Protocol violation by server. */
		warn0("Network protocol violation by server");
		break;
	}
}

/**
 * netproto_alloc(callback, cookie):
 * Allocate a network protocol connection cookie.  If the connection is closed
 * before netproto_setfd is called, netproto_close will call callback(cookie)
 * in lieu of performing callback cancels on a socket.
 */
struct netproto_connection_internal *
netproto_alloc(int (* callback)(void *), void * cookie)
{
	struct netproto_connection_internal * C;

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct netproto_connection_internal))) == NULL)
		goto err0;

	/* Record connect-cancel callback and cookie. */
	C->cancel = callback;
	C->cookie = cookie;

	/* We have no state yet. */
	C->fd = -1;
	C->keys = NULL;
	C->sleepcookie.handle = -1;
	C->bytesin = C->bytesout = C->bytesqueued = 0;
	C->broken = 0;
	C->Q = NULL;

	/* Success! */
	return (C);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * netproto_setfd(C, fd):
 * Set the network protocol connection cookie ${C} to use connected socket
 * ${fd}.  This function must be called exactly once after netproto_alloc
 * before calling any other functions aside from netproto_free.
 */
int
netproto_setfd(struct netproto_connection_internal * C, int fd)
{

	/* The connect is no longer pending. */
	C->cancel = NULL;
	C->cookie = NULL;

	/* We have a file descriptor. */
	C->fd = fd;

	/* Create a network layer write queue. */
	if ((C->Q = network_writeq_init(fd)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * netproto_getstats(C, in, out, queued):
 * Obtain the number of bytes received and sent via the connection, and the
 * number of bytes ${queued} to be written.
 */
void
netproto_getstats(NETPROTO_CONNECTION * C, uint64_t * in, uint64_t * out,
    uint64_t * queued)
{

	*in = C->bytesin;
	*out = C->bytesout;
	*queued = C->bytesqueued;
}

/**
 * netproto_sleep(C, secs, callback, cookie):
 * Call the provided callback after ${secs} seconds.
 */
int
netproto_sleep(NETPROTO_CONNECTION * C, int secs,
    network_callback * callback, void * cookie)
{
	struct timeval timeo;

	/* Set timeout. */
	timeo.tv_sec = secs;
	timeo.tv_usec = 0;

	/* Make sure this connection isn't already sleeping. */
	if (C->sleepcookie.handle != -1) {
		warn0("Connection is already sleeping!");
		goto err0;
	}

	/* Record callback parameters. */
	C->sleepcookie.callback = callback;
	C->sleepcookie.cookie = cookie;

	/* Ask for a wake-up call. */
	if ((C->sleepcookie.handle =
	    network_sleep(&timeo, callback_sleep, C)) == -1)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * callback_sleep(cookie, status):
 * Helper function for netproto_sleep.
 */
static int
callback_sleep(void * cookie, int status)
{
	NETPROTO_CONNECTION * C = cookie;

	/*
	 * Our wake-up call is happening right now; record that there is no
	 * pending callback so that we don't try to deregister it when the
	 * connection is closed later.
	 */
	C->sleepcookie.handle = -1;

	/* Call the requested callback. */
	return ((C->sleepcookie.callback)(C->sleepcookie.cookie, status));
}

/**
 * netproto_flush(C):
 * Cancel all pending writes and any in-progress read.
 */
int
netproto_flush(NETPROTO_CONNECTION * C)
{
	int rc, rc2;

	/* Cancel pending writes. */
	if (C->Q != NULL)
		rc = network_writeq_cancel(C->Q);
	else
		rc = 0;

	/*
	 * Mark this connection as being broken.  The upstream caller should
	 * never try to write any packets after calling netproto_flush --
	 * this allows us to detect and print a warning if it does.
	 */
	C->broken = 1;

	/* Cancel any in-progress read. */
	if (C->fd != -1) {
		rc2 = network_deregister(C->fd, NETWORK_OP_READ);
		if (rc == 0)
			rc = rc2;
	}

	/* Return success or the first nonzero callback value. */
	return (rc);
}

/**
 * netproto_close(C):
 * Cancel all pending writes and any in-progress read, and free memory.
 */
int
netproto_close(NETPROTO_CONNECTION * C)
{
	int rc, rc2;

	/* If we were connecting, cancel that. */
	if (C->cancel != NULL)
		rc = (C->cancel)(C->cookie);
	else
		rc = 0;

	/* Cancel pending writes. */
	if (C->Q != NULL) {
		rc2 = network_writeq_cancel(C->Q);
		rc = rc ? rc : rc2;
	}

	/* Free the write queue. */
	if (C->Q != NULL)
		network_writeq_free(C->Q);

	/* Cancel any in-progress read. */
	if (C->fd != -1) {
		rc2 = network_deregister(C->fd, NETWORK_OP_READ);
		rc = rc ? rc : rc2;
	}

	/* Free cryptographic keys, if any exist. */
	crypto_session_free(C->keys);

	/* Close the socket. */
	while (C->fd != -1 && close(C->fd)) {
		if (errno == ECONNRESET) {
			/*
			 * You can't dump me!  I'm dumping you!  We don't
			 * care about the connection dying since we're
			 * done with it anyway.
			 */
			break;
		}

		if (errno != EINTR) {
			warnp("close()");
			goto err1;
		}
	}

	/* Free the network protocol cookie. */
	free(C);

	/* Return success or the first nonzero callback value. */
	return (rc);

err1:
	free(C);

	/* Failure! */
	return (-1);
}
