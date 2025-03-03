#include <stdio.h>

#include "warnp.h"

#include "print_separator.h"

/**
 * print_separator(stream, separator, print_nulls, num_nulls):
 * Print ${separator} to ${stream} if ${print_nulls} is zero; otherwise,
 * print ${num_nulls} '\0'.
 */
int
print_separator(FILE * stream, const char * separator, int print_nulls,
    int num_nulls)
{
	int i;

	/* Are we printing normal separators, or NULs? */
	if (print_nulls == 0) {
		if (fprintf(stream, "%s", separator) < 0) {
			warnp("fprintf");
			goto err0;
		}
	} else {
		for (i = 0; i < num_nulls; i++) {
			if (fprintf(stream, "%c", '\0') < 0) {
				warnp("fprintf");
				goto err0;
			}
		}
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
