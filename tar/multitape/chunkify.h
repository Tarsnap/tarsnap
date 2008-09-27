#ifndef _CHUNKIFY_H_
#define _CHUNKIFY_H_

#include <stdint.h>
#include <stdlib.h>

/**
 * Structure used to store chunkifier state.
 */
typedef struct chunkifier_internal CHUNKIFIER;

/**
 * Callback when the end of a chunk is reached.  The parameters passed are
 * the cookie provided to chunkify_init, a pointer to a buffer containing
 * the chunk, and the length of the chunk in bytes.
 *
 * Upon success, the callback should return 0.  Upon failure, a nonzero
 * value should be returned, and will be passed upstream to the caller of
 * chunkify_write or chunkify_end.
 */
typedef int chunkify_callback(void *, uint8_t *, size_t);

/**
 * chunkify_init(meanlen, maxlen, callback, cookie)
 * initializes and returns a CHUNKIFIER structure suitable for dividing a
 * stream of bytes into chunks of mean and maximum lengths meanlen and maxlen.
 * In most cases, maxlen should be at least 2 * meanlen; values greater than
 * 4 * meanlen will have very little effect beyond wasting memory.
 *
 * If an error occurs, NULL is returned.
 *
 * The parameter meanlen must be at most 1262226.
 * The probability of a chunk being x bytes or longer is approximately
 * 0.267 ^ ((x / meanlen)^(3/2)).
 * The most common chunk length is approximately 0.65 * meanlen.
 */
CHUNKIFIER * chunkify_init(uint32_t, uint32_t, chunkify_callback *, void *);

/**
 * chunkify_write(c, buf, buflen)
 * feeds the provided buffer into the CHUNKIFIER; callback(s) are made if
 * chunk(s) end during this process.
 *
 * The value returned is zero, of the first nonzero value returned by the
 * callback function.
 */
int chunkify_write(CHUNKIFIER *, const uint8_t *, size_t);

/**
 * chunkify_end(c)
 * terminates a chunk by calling chunkdone and initializing the CHUNKIFIER
 * for the start of the next chunk.
 *
 * The value returned is zero or the nonzero value returned by the callback
 * function.
 */
int chunkify_end(CHUNKIFIER *);

/**
 * chunkify_free(c)
 * frees the memory allocated by chunkify_init(...), but does not
 * call chunkify_end; the caller is responsible for doing this.
 */
void chunkify_free(CHUNKIFIER *);

#endif /* !_CHUNKIFY_H_ */
