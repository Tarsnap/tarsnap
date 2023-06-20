#ifndef TVMATH_H_
#define TVMATH_H_

#include <sys/time.h>

/**
 * tvmath_addctime(tv):
 * Set tv += monoclock.
 */
int tvmath_addctime(struct timeval *);

/**
 * tvmath_subctime(tv):
 * Set tv -= monoclock.
 */
int tvmath_subctime(struct timeval *);

/**
 * tvmath_rsubctime(tv):
 * Set tv = monoclock - tv.
 */
int tvmath_rsubctime(struct timeval *);

#endif /* !TVMATH_H_ */
