#include <stdio.h>
#include <string.h>

#include "warnp.h"

#include "readpass_file.h"

#define MAXPASSLEN 2048

/**
 * readpass_file(passwd, filename):
 * Read a password from ${filename}, returning it as a malloced NUL-terminated
 * string via ${passwd}.  Ignore any data after a newline character, if one
 * is present.  Fail if the file contains more than one line, or if the line
 * is longer than MAXPASSLEN.
 */
int
readpass_file(char ** passwd, const char * filename)
{
	FILE * f;
	char passbuf[MAXPASSLEN];
	size_t endpos;
	int ch;

	/* Open the file. */
	if ((f = fopen(filename, "r")) == NULL) {
		warnp("fopen(%s)", filename);
		goto err0;
	}

	/* Read the password. */
	if ((fgets(passbuf, MAXPASSLEN, f)) == NULL) {
		if (ferror(f)) {
			warnp("fgets(%s)", filename);
			goto err2;
		} else {
			/* We have a 0-byte password. */
			passbuf[0] = '\0';
		}
	}

	/* Truncate at the first "\r" or "\n" (if any). */
	endpos = strcspn(passbuf, "\r\n");
	passbuf[endpos] = '\0';

	/* Check for more than 1 line, or line too long. */
	ch = fgetc(f);
	if (ch != EOF) {
		warn0("line too long, or more than 1 line in %s", filename);
		goto err2;
	} else if (ferror(f)) {
		warnp("fgetc(%s)", filename);
		goto err2;
	}

	/* Close the file. */
	if (fclose(f)) {
		warnp("fclose(%s)", filename);
		goto err1;
	}
	f = NULL;

	/* Copy the password out. */
	if ((*passwd = strdup(passbuf)) == NULL) {
		warnp("strdup");
		goto err1;
	}

	/* Success! */
	return (0);

err2:
	if (fclose(f))
		warnp("fclose(%s)", filename);
err1:
	f = NULL;
err0:
	/* Failure! */
	return (-1);
}
