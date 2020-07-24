#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "warnp.h"

static int initialized = 0;
static char * name = NULL;
static int use_syslog = 0;
static int syslog_priority = LOG_WARNING;

/* Free the name string. */
static void
done(void)
{

	free(name);
	name = NULL;
}

/**
 * warnp_setprogname(progname):
 * Set the program name to be used by warn() and warnx() to ${progname}.
 */
void
warnp_setprogname(const char * progname)
{
	const char * p;

	/* Free the name if we already have one. */
	free(name);

	/* Find the last segment of the program name. */
	for (p = progname; progname[0] != '\0'; progname++)
		if (progname[0] == '/')
			p = progname + 1;

	/* Copy the name string. */
	name = strdup(p);

	/* If we haven't already done so, register our exit handler. */
	if (initialized == 0) {
		atexit(done);
		initialized = 1;
	}
}

void
warn(const char * fmt, ...)
{
	va_list ap;
	char msgbuf[WARNP_SYSLOG_MAX_LINE + 1];

	va_start(ap, fmt);
	if (use_syslog == 0) {
		/* Print to stderr. */
		fprintf(stderr, "%s", (name != NULL) ? name : "(unknown)");
		if (fmt != NULL) {
			fprintf(stderr, ": ");
			vfprintf(stderr, fmt, ap);
		}
		fprintf(stderr, ": %s\n", strerror(errno));
	} else {
		/* Print to syslog. */
		if (fmt != NULL) {
			/* No need to print "${name}: "; syslog does it. */
			vsnprintf(msgbuf, WARNP_SYSLOG_MAX_LINE + 1, fmt, ap);
			syslog(syslog_priority, "%s: %s\n", msgbuf,
			    strerror(errno));
		} else
			syslog(syslog_priority, "%s\n", strerror(errno));
	}
	va_end(ap);
}

void
warnx(const char * fmt, ...)
{
	va_list ap;
	char msgbuf[WARNP_SYSLOG_MAX_LINE + 1];

	va_start(ap, fmt);
	if (use_syslog == 0) {
		/* Print to stderr. */
		fprintf(stderr, "%s", (name != NULL) ? name : "(unknown)");
		if (fmt != NULL) {
			fprintf(stderr, ": ");
			vfprintf(stderr, fmt, ap);
		}
		fprintf(stderr, "\n");
	} else {
		/* Print to syslog. */
		if (fmt != NULL) {
			/* No need to print "${name}: "; syslog does it. */
			vsnprintf(msgbuf, WARNP_SYSLOG_MAX_LINE + 1, fmt, ap);
			syslog(syslog_priority, "%s\n", msgbuf);
		} else
			syslog(syslog_priority, "\n");
	}
	va_end(ap);
}
