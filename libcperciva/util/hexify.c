#include <stdint.h>
#include <string.h>

#include "hexify.h"

static char hexchars[] = "0123456789abcdef0123456789ABCDEF";

/**
 * hexify(in, out, len):
 * Convert ${len} bytes from ${in} into hexadecimal, writing the resulting
 * 2 * ${len} bytes to ${out}; and append a NUL byte.
 */
void
hexify(const uint8_t * in, char * out, size_t len)
{
	char * p = out;
	size_t i;

	for (i = 0; i < len; i++) {
		*p++ = hexchars[in[i] >> 4];
		*p++ = hexchars[in[i] & 0x0f];
	}
	*p = '\0';
}

/**
 * unhexify(in, out, len):
 * Convert 2 * ${len} hexadecimal characters from ${in} to ${len} bytes
 * and write them to ${out}.  This function will only fail if the input is
 * not a sequence of hexadecimal characters.
 */
int
unhexify(const char * in, uint8_t * out, size_t len)
{
	size_t i;

	/* Make sure we have at least 2 * ${len} hex characters. */
	for (i = 0; i < 2 * len; i++) {
		if ((in[i] == '\0') || (strchr(hexchars, in[i]) == NULL))
			goto err0;
	}

	for (i = 0; i < len; i++) {
		out[i] = (strchr(hexchars, in[2 * i]) - hexchars) & 0x0f;
		out[i] <<= 4;
		out[i] += (strchr(hexchars, in[2 * i + 1]) - hexchars) & 0x0f;
	}

	/* Success! */
	return (0);

err0:
	/* Bad input string. */
	return (-1);
}
