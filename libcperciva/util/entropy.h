#ifndef _ENTROPY_H_
#define _ENTROPY_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque type. */
struct entropy_read_cookie;

/**
 * entropy_read_init(void):
 * Initialize the ability to produce random bytes from the operating system,
 * and return a cookie.
 */
struct entropy_read_cookie * entropy_read_init(void);

/**
 * entropy_read_fill(er, buf, buflen):
 * Fill the given buffer with random bytes provided by the operating system
 * using the resources in ${er}.
 */
int entropy_read_fill(struct entropy_read_cookie *, uint8_t *, size_t);

/**
 * entropy_read_done(er):
 * Release any resources used by {er}.
 */
int entropy_read_done(struct entropy_read_cookie * er);

/**
 * entropy_read(buf, buflen):
 * Fill the given buffer with random bytes provided by the operating system.
 */
int entropy_read(uint8_t *, size_t);

#endif /* !_ENTROPY_H_ */
