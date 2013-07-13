#ifndef _HEXIFY_H_
#define _HEXIFY_H_

#include <stddef.h>
#include <stdint.h>

/**
 * hexify(in, out, len):
 * Convert ${len} bytes from ${in} into hexadecimal, writing the resulting
 * 2 * ${len} bytes to ${out}; and append a NUL byte.
 */
void hexify(const uint8_t *, char *, size_t);

/**
 * unhexify(in, out, len):
 * Convert 2 * ${len} hexadecimal characters from ${in} to ${len} bytes
 * and write them to ${out}.  This function will only fail if the input is
 * not a sequence of hexadecimal characters.
 */
int unhexify(const char *, uint8_t *, size_t);

#endif /* !_HEXIFY_H_ */
