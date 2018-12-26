#ifndef _MONOCLOCK_H_
#define _MONOCLOCK_H_

#include <sys/time.h>

#define timeval_diff(x, y) ((y.tv_sec - x.tv_sec) +			\
    (y.tv_usec - x.tv_usec) * 0.000001)

/**
 * monoclock_get(tv):
 * Store the current time in ${tv}.  If CLOCK_MONOTONIC is available, use
 * that clock; if CLOCK_MONOTONIC is unavailable, use CLOCK_REALTIME (if
 * available) or gettimeofday(2).
 */
int monoclock_get(struct timeval *);

/**
 * monoclock_get_cputime(tv):
 * Store in ${tv} the duration the process has been running if
 * CLOCK_PROCESS_CPUTIME_ID is available; fall back to monoclock_get()
 * otherwise.
 */
int monoclock_get_cputime(struct timeval *);

#endif /* !_MONOCLOCK_H_ */
