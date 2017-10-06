#include "bsdtar_platform.h"

#include <sys/time.h>

#include "monoclock.h"

#include "tvmath.h"

/**
 * tvmath_addctime(tv):
 * Set tv += monoclock.
 */
int
tvmath_addctime(struct timeval * tv)
{
	struct timeval tnow;

	if (monoclock_get(&tnow))
		goto err0;
	tv->tv_sec += tnow.tv_sec;
	tv->tv_usec += tnow.tv_usec;
	if (tv->tv_usec >= 1000000) {
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
 * tvmath_subctime(tv):
 * Set tv -= monoclock.
 */
int
tvmath_subctime(struct timeval * tv)
{
	struct timeval tnow;

	if (monoclock_get(&tnow))
		goto err0;
	tv->tv_sec -= tnow.tv_sec;
	tv->tv_usec -= tnow.tv_usec;
	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec -= 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * tvmath_rsubctime(tv):
 * Set tv = monoclock - tv.
 */
int
tvmath_rsubctime(struct timeval * tv)
{
	struct timeval tnow;

	if (monoclock_get(&tnow))
		goto err0;
	tv->tv_sec = tnow.tv_sec - tv->tv_sec;
	tv->tv_usec = tnow.tv_usec - tv->tv_usec;
	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec -= 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
