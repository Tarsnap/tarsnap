#include "bsdtar_platform.h"

#include <errno.h>
#include <stdlib.h>

#include "network.h"
#include "warnp.h"

#include "netproto.h"
#include "netproto_internal.h"

static network_callback callback_sleep;

/**
 * _netproto_printerr(status):
 * Print the error message associated with the given status code.
 */
void
_netproto_printerr(int status)
{

	switch (status) {
	case NETWORK_STATUS_CONNERR:
		/* Could not connect; error is specified in errno. */
		warnp("Error connecting to server");
		break;
	case NETWORK_STATUS_ERR:
		/* Error is specified in errno. */
		warnp("Network error");
		break;
	case NETWORK_STATUS_NODATA:
	case NETWORK_STATUS_TIMEOUT:
		/* Server timed out. */
		warn0("Timeout communicating with server");
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
 * netproto_open(fd):
 * Create and return a network protocol connection cookie for use on the
 * connected socket ${fd}.  Note that netproto_keyexchange() must be called
 * before _writepacket or _readpacket are called on the cookie.
 */
NETPROTO_CONNECTION *
netproto_open(int fd)
{
	struct netproto_connection_internal * C;

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct netproto_connection_internal))) == NULL)
		goto err0;

	/* We have a file descriptor, but we don't have keys yet. */
	C->fd = fd;
	C->keys = NULL;

	/* We're not sleeping. */
	C->sleepcookie.handle = -1;

	/* No traffic yet. */
	C->bytesin = C->bytesout = C->bytesqueued = 0;

	/* The connection isn't broken. */
	C->broken = 0;

	/* Create a network layer write queue. */
	if ((C->Q = network_writeq_init(fd)) == NULL)
		goto err1;

	/* Success! */
	return (C);

err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * netproto_getstats(C, in, out, queued):
 * Obtain the number of bytes received and sent via the connection, and the
 * number of bytes queued to be written.
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
	 * pending callback so that we don't try to unregister it when the
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
	rc = network_writeq_cancel(C->Q);

	/*
	 * Mark this connection as being broken.  The upstream caller should
	 * never try to write any packets after calling netproto_flush --
	 * this allows us to detect and print a warning if it does.
	 */
	C->broken = 1;

	/* Cancel any in-progress read. */
	rc2 = network_deregister(C->fd, NETWORK_OP_READ);
	if (rc == 0)
		rc = rc2;

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

	/* Cancel any in-progress sleep. */
	if (C->sleepcookie.handle != -1)
		network_desleep(C->sleepcookie.handle);

	/* Cancel pending writes. */
	rc = network_writeq_cancel(C->Q);

	/* Free the write queue. */
	network_writeq_free(C->Q);

	/* Cancel any in-progress read. */
	rc2 = network_deregister(C->fd, NETWORK_OP_READ);
	rc = rc ? rc : rc2;

	/* Free cryptographic keys, if any exist. */
	crypto_session_free(C->keys);

	/* Close the socket. */
	while (close(C->fd)) {
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
