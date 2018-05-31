#include <sys/time.h>

#include <errno.h>
#include <time.h>

#include "warnp.h"

#include "monoclock.h"

/* Determine which clock(s) to use. */
#ifndef POSIXFAIL_CLOCK_GETTIME
#ifdef CLOCK_MONOTONIC
#define USE_MONOTONIC
#endif
#ifndef POSIXFAIL_CLOCK_REALTIME
#define USE_REALTIME
#endif
#endif

/**
 * monoclock_get(tv):
 * Store the current time in ${tv}.  If CLOCK_MONOTONIC is available, use
 * that clock; if CLOCK_MONOTONIC is unavailable, use CLOCK_REALTIME (if
 * available) or gettimeofday(2).
 */
int
monoclock_get(struct timeval * tv)
{
#if defined(USE_MONOTONIC) || defined(USE_REALTIME)
	struct timespec tp;
#endif

#ifdef USE_MONOTONIC
	if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0) {
		tv->tv_sec = tp.tv_sec;
		tv->tv_usec = (suseconds_t)(tp.tv_nsec / 1000);
	} else if ((errno != ENOSYS) && (errno != EINVAL)) {
		warnp("clock_gettime(CLOCK_MONOTONIC)");
		goto err0;
	} else
#endif
#ifdef USE_REALTIME
	if (clock_gettime(CLOCK_REALTIME, &tp) == 0) {
		tv->tv_sec = tp.tv_sec;
		tv->tv_usec = (suseconds_t)(tp.tv_nsec / 1000);
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

/**
 * monoclock_get_cputime(tv):
 * Store in ${tv} the duration the process has been running if
 * CLOCK_PROCESS_CPUTIME_ID is available; fall back to monoclock_get()
 * otherwise.
 */
int
monoclock_get_cputime(struct timeval * tv)
{
	/* Use CLOCK_PROCESS_CPUTIME_ID if available. */
#ifdef CLOCK_PROCESS_CPUTIME_ID
	struct timespec tp;

	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp) == 0) {
		tv->tv_sec = tp.tv_sec;
		tv->tv_usec = (suseconds_t)(tp.tv_nsec / 1000);
	} else if ((errno != ENOSYS) && (errno != EINVAL)) {
		warnp("clock_gettime(CLOCK_PROCESS_CPUTIME_ID)");
		goto err0;
	} else
#endif
	/* Fall back to monoclock_get(). */
	if (monoclock_get(tv))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
