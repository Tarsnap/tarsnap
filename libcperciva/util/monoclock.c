#include <sys/time.h>

#include <time.h>

#include "warnp.h"

#include "monoclock.h"

/**
 * monoclock_get(tv):
 * Store the current time in ${tv}.  If CLOCK_MONOTONIC is available, use
 * that clock; if CLOCK_MONOTONIC is unavailable, use CLOCK_REALTIME (if
 * available) or gettimeofday(2).
 */
int
monoclock_get(struct timeval * tv)
{
#if defined(CLOCK_MONOTONIC) || !defined(POSIXFAIL_CLOCK_REALTIME)
	struct timespec tp;
#endif

#ifdef CLOCK_MONOTONIC
	if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0) {
		tv->tv_sec = tp.tv_sec;
		tv->tv_usec = tp.tv_nsec / 1000;
	} else if ((errno != ENOSYS) && (errno != EINVAL)) {
		warnp("clock_gettime(CLOCK_MONOTONIC)");
		goto err0;
	} else
#endif
#ifndef POSIXFAIL_CLOCK_REALTIME
	if (clock_gettime(CLOCK_REALTIME, &tp) == 0) {
		tv->tv_sec = tp.tv_sec;
		tv->tv_usec = tp.tv_nsec / 1000;
	} else {
		warnp("clock_gettime(CLOCK_REALTIME)");
		goto err0;
	}
#else
	if (gettimeofday(tv, NULL)) {
		warnp("gettimeofday");
		goto err0;
	}
#endif

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
