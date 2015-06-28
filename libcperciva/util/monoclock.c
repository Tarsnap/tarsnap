#include <sys/time.h>

#include <time.h>

#include "warnp.h"

#include "monoclock.h"

/**
 * monoclock_get(tv):
 * Store the current time in ${tv}.  If CLOCK_MONOTONIC is available, use
 * that clock; otherwise, use gettimeofday(2).
 */
int
monoclock_get(struct timeval * tv)
{
#ifdef CLOCK_MONOTONIC
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
	if (gettimeofday(tv, NULL)) {
		warnp("gettimeofday");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
