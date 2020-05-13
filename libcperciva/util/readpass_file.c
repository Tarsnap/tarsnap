#include <stdio.h>
#include <string.h>

#include "insecure_memzero.h"
#include "warnp.h"

#include "readpass.h"

/* Maximum file length. */
#define MAXPASSLEN 2048

/**
 * readpass_file(passwd, filename):
 * Read a passphrase from ${filename} and return it as a malloced
 * NUL-terminated string via ${passwd}.  Print an error and fail if the file
 * is 2048 characters or more, or if it contains any newline \n or \r\n
 * characters other than at the end of the file.  Do not include the \n or
 * \r\n characters in the passphrase.
 */
int
readpass_file(char ** passwd, const char * filename)
{
	FILE * f;
	char passbuf[MAXPASSLEN];

	/* Open the file. */
	if ((f = fopen(filename, "r")) == NULL) {
		warnp("fopen(%s)", filename);
		goto err1;
	}

	/* Get a line from the file. */
	if ((fgets(passbuf, MAXPASSLEN, f)) == NULL) {
		if (ferror(f)) {
			warnp("fread(%s)", filename);
			goto err2;
		} else {
			/* We have a 0-byte password. */
			passbuf[0] = '\0';
		}
	}

	/* Bail if there's the line is too long, or if there's a second line. */
	if (fgetc(f) != EOF) {
		warn0("line too long, or more than 1 line in %s", filename);
		goto err2;
	}

	/* Close the file. */
	if (fclose(f)) {
		warnp("fclose(%s)", filename);
		goto err1;
	}

	/* Copy the password out. */
	if ((*passwd = strdup(passbuf)) == NULL) {
		warnp("Cannot allocate memory");
		goto err1;
	}

	/* Clean up. */
	insecure_memzero(passbuf, MAXPASSLEN);

	/* Success! */
	return (0);

err2:
	fclose(f);
err1:
	/* No harm in running this for all error paths. */
	insecure_memzero(passbuf, MAXPASSLEN);

	/* Failure! */
	return (-1);
}
