#include "bsdtar_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "hexify.h"
#include "warnp.h"

#include "hexlink.h"

/**
 * hexlink_write(path, buf, buflen):
 * Convert ${buf} (of length ${buflen}) into hexadecimal and create a link
 * from ${path} pointing at it.
 */
int
hexlink_write(const char * path, const uint8_t * buf, size_t buflen)
{
	char * hexbuf;

	/* Allocate memory for buffer. */
	if ((hexbuf = malloc(buflen * 2 + 1)) == NULL)
		goto err0;

	/* Convert ${buf} to hex. */
	hexify(buf, hexbuf, buflen);

	/* Create the symlink. */
	if (symlink(hexbuf, path)) {
		warnp("symlink(%s, %s)", hexbuf, path);
		goto err1;
	}

	/* Free allocated memory. */
	free(hexbuf);

	/* Success! */
	return (0);

err1:
	free(hexbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * hexlink_read(path, buf, buflen):
 * Read the link ${path}, which should point to a hexadecimal string of
 * length 2 * ${buflen}; and parse this into the provided buffer.  In the
 * event of an error, return with errno == ENOENT iff the link does not
 * exist.
 */
int
hexlink_read(const char * path, uint8_t * buf, size_t buflen)
{
	char * hexbuf;
	ssize_t rc;

	/* Allocate memory for buffer. */
	if ((hexbuf = malloc(buflen * 2 + 1)) == NULL)
		goto err0;

	/* Attempt to read the link. */
	if ((rc = readlink(path, hexbuf, buflen * 2)) == -1) {
		/* Couldn't read the link. */
		goto err1;
	}

	/* Is the link the correct length? */
	if ((size_t)rc != (buflen * 2)) {
		warn0("Link is incorrect length: %s", path);
		goto err1;
	}

	/* NUL-terminate. */
	hexbuf[rc] = '\0';

	/* Parse the link value into the provided buffer. */
	if (unhexify(hexbuf, buf, buflen)) {
		warn0("Cannot parse link as hexadecimal: %s -> %s",
		    path, hexbuf);
		goto err1;
	}

	/* Free allocated memory. */
	free(hexbuf);

	/* Success! */
	return (0);

err1:
	free(hexbuf);
err0:
	/* Failure! */
	return (-1);
}
