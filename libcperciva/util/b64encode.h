#ifndef _B64ENCODE_H_
#define _B64ENCODE_H_

#include <stddef.h>
#include <stdint.h>

/* The resulting length after base-64 encoding, not including the NUL byte. */
#define b64len(origlen) (((origlen + 2) / 3) * 4)

/**
 * b64encode(in, out, len):
 * Convert ${len} bytes from ${in} into RFC 1421 base-64 encoding, writing
 * the resulting ((len + 2) / 3) * 4 bytes to ${out}; and append a NUL byte.
 */
void b64encode(const uint8_t *, char *, size_t);

/**
 * b64decode(in, inlen, out, outlen):
 * Convert ${inlen} bytes of RFC 1421 base-64 encoding from ${in}, writing
 * the resulting bytes to ${out}; and pass the number of bytes output back
 * via ${outlen}.  The buffer ${out} must contain at least (inlen/4)*3 bytes
 * of space; but ${outlen} might be less than this.  Return non-zero if the
 * input ${in} is not valid base-64 encoded text.
 */
int b64decode(const char *, size_t, uint8_t *, size_t *);

#endif /* !_B64ENCODE_H_ */
