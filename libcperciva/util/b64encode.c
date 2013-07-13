#include <stdint.h>
#include <string.h>

#include "b64encode.h"

static char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

/**
 * b64encode(in, out, len):
 * Convert ${len} bytes from ${in} into RFC 1421 base-64 encoding, writing
 * the resulting ((len + 2) / 3) * 4 bytes to ${out}; and append a NUL byte.
 */
void
b64encode(const uint8_t * in, char * out, size_t len)
{
	uint32_t t;
	size_t j;

	/* Repeat {read up to 3 bytes; write 4 bytes} until we're done. */
	while (len) {
		/* Read up to 3 bytes. */
		for (t = j = 0; j < 3; j++) {
			t <<= 8;
			if (j < len)
				t += *in++;
		}

		/* Write 4 bytes. */
		for (j = 0; j < 4; j++) {
			if (j <= len)
				*out++ = b64chars[(t >> 18) & 0x3f];
			else
				*out++ = '=';
			t <<= 6;
		}

		/* Adjust the remaining length. */
		if (len < 3)
			len = 0;
		else
			len -= 3;
	}

	/* NUL terminate. */
	*out++ = '\0';
}

/**
 * b64decode(in, inlen, out, outlen):
 * Convert ${inlen} bytes of RFC 1421 base-64 encoding from ${in}, writing
 * the resulting bytes to ${out}; and pass the number of bytes output back
 * via ${outlen}.  The buffer ${out} must contain at least (inlen/4)*3 bytes
 * of space; but ${outlen} might be less than this.  Return non-zero if the
 * input ${in} is not valid base-64 encoded text.
 */
int
b64decode(const char * in, size_t inlen, uint8_t * out, size_t * outlen)
{
	uint32_t t;
	size_t deadbytes = 0;
	size_t i;

	/* We must have a multiple of 4 input bytes. */
	if (inlen % 4 != 0)
		goto bad;

	/* Check that we have valid input and count trailing '='s. */
	for (i = 0; i < inlen; i++) {
		/* Must be characters from our b64 character set. */
		if ((in[i] == '\0') || (strchr(b64chars, in[i]) == NULL))
			goto bad;

		/* Is this a '=' character? */
		if (in[i] == '=')
			deadbytes += 1;

		/* No non-'=' bytes after a '=' byte. */
		if ((in[i] != '=') && (deadbytes > 0))
			goto bad;
	}

	/* Can't have more than 2 trailing '=' bytes. */
	if (deadbytes > 2)
		goto bad;

	/* We have no output yet. */
	*outlen = 0;

	/* Loop until we run out of data. */
	while (inlen) {
		/* Parse 4 bytes. */
		for (t = 0, i = 0; i < 4; i++) {
			t <<= 6;
			t += (strchr(b64chars, in[i]) - b64chars) & 0x3f;
		}

		/* Output 3 bytes. */
		for (i = 0; i < 3; i++) {
			out[i] = t >> 16;
			t <<= 8;
		}

		/* Adjust pointers and lengths for the completed block. */
		in += 4;
		inlen -= 4;
		out += 3;
		*outlen += 3;
	}

	/* Ignore dead bytes. */
	*outlen -= deadbytes;

	/* Success! */
	return (0);

bad:
	/* The input is not valid base-64 encoded text. */
	return (1);
}
