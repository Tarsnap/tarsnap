#ifndef _MONOCLOCK_H_
#define _MONOCLOCK_H_

#include <sys/time.h>

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
