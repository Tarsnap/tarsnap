#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "warnp.h"

#include "tsnetwork_internal.h"

/**
 * XXX Portability
 * XXX These functions serve two purposes:
 * XXX 1. To avoid wasting bandwidth, by ensuring that multiple small writes
 * XXX over a socket are aggregated into a single TCP/IP packet.
 * XXX 2. To avoid severe performance issues which would otherwise result
 * XXX from nagling, by allowing data to be "pushed" out once there are no
 * XXX more writes queued.
 * XXX
 * XXX POSIX defines TCP_NODELAY for purpose #2, although it does not require
 * XXX that implementations obey it; BSD and Linux respectively define
 * XXX TCP_NOPUSH and TCP_CORK for purpose #1.  On OS X, TCP_NOPUSH is
 * XXX defined, but seems to be broken; we use autoconf to detect OS X and
 * XXX define BROKEN_TCP_NOPUSH.  On Cygwin, TCP_NOPUSH is defined, but
 * XXX using it produces a ENOPROTOOPT error; we define BROKEN_TCP_NOPUSH
 * XXX in this case, too.  On Minix, TCP_NODELAY fails with ENOSYS; since
 * XXX corking occurs for performance reasons only, we ignore this errno.
 */

/* Macro to simplify setting options. */
#define setopt(fd, opt, value, err0) do {				\
	int val;							\
									\
	val = value;							\
	if (setsockopt(fd, IPPROTO_TCP, opt, &val, sizeof(int))) {	\
		if ((errno != ETIMEDOUT) &&				\
		    (errno != ECONNRESET) &&				\
		    (errno != ENOSYS)) {				\
			warnp("setsockopt(%s, %d)", #opt, val);		\
			goto err0;					\
		}							\
	}								\
} while (0);

/**
 * network_cork(fd):
 * Clear the TCP_NODELAY socket option, and set TCP_CORK or TCP_NOPUSH if
 * either is defined.
 */
int
network_cork(int fd)
{

	/* Clear TCP_NODELAY. */
	setopt(fd, TCP_NODELAY, 0, err0);

	/* Set TCP_CORK or TCP_NOPUSH as appropriate. */
#ifdef TCP_CORK
	setopt(fd, TCP_CORK, 1, err0);
#else
#ifdef TCP_NOPUSH
#ifndef BROKEN_TCP_NOPUSH
	setopt(fd, TCP_NOPUSH, 1, err0);
#endif
#endif
#endif

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_uncork(fd):
 * Set the TCP_NODELAY socket option, and clear TCP_CORK or TCP_NOPUSH if
 * either is defined.
 */
int
network_uncork(int fd)
{

	/* Clear TCP_CORK or TCP_NOPUSH as appropriate. */
#ifdef TCP_CORK
	setopt(fd, TCP_CORK, 0, err0);
#else
#ifdef TCP_NOPUSH
#ifndef BROKEN_TCP_NOPUSH
	setopt(fd, TCP_NOPUSH, 0, err0);
#endif
#endif
#endif

	/* Set TCP_NODELAY. */
	/*
	 * For compatibility with Linux 2.4, this must be done after we
	 * clear TCP_CORK; otherwise it will throw an EINVAL back at us.
	 */
	setopt(fd, TCP_NODELAY, 1, err0);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
