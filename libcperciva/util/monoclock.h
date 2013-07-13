#ifndef _MONOCLOCK_H_
#define _MONOCLOCK_H_

#include <sys/time.h>

/**
 * monoclock_get(tv):
 * Store the current time in ${tv}.  If CLOCK_MONOTONIC is available, use
 * that clock; otherwise, use gettimeofday(2).
 */
int monoclock_get(struct timeval *);

#endif /* !_MONOCLOCK_H_ */
