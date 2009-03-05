#ifdef HAVE_ERR_H
/*
 * Everything is provided through err.h and the associated library, so we
 * don't need to do anything here.
 */
#else
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

const char * warn_progname = "(null)";

void
warn(const char * fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s", warn_progname);
	if (fmt != NULL) {
		fprintf(stderr, ": ");
		vfprintf(stderr, fmt, ap);
	}
	fprintf(stderr, ": %s\n", strerror(errno));
	va_end(ap);
}

void
warnx(const char * fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s", warn_progname);
	if (fmt != NULL) {
		fprintf(stderr, ": ");
		vfprintf(stderr, fmt, ap);
	}
	fprintf(stderr, "\n");
	va_end(ap);
}
#endif
