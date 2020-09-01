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

/* Free the name string and clean up writing to the syslog (if applicable). */
static void
done(void)
{

	/* Clean up writing to the syslog (if applicable).  */
	if (use_syslog)
		closelog();

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
		/* Stop other threads writing to stderr. */
		flockfile(stderr);

		/* Print to stderr. */
		fprintf(stderr, "%s", (name != NULL) ? name : "(unknown)");
		if (fmt != NULL) {
			fprintf(stderr, ": ");
			vfprintf(stderr, fmt, ap);
		}
		fprintf(stderr, ": %s\n", strerror(errno));

		/* Allow other threads to write to stderr. */
		funlockfile(stderr);
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
		/* Stop other threads writing to stderr. */
		flockfile(stderr);

		/* Print to stderr. */
		fprintf(stderr, "%s", (name != NULL) ? name : "(unknown)");
		if (fmt != NULL) {
			fprintf(stderr, ": ");
			vfprintf(stderr, fmt, ap);
		}
		fprintf(stderr, "\n");

		/* Allow other threads to write to stderr. */
		funlockfile(stderr);
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

/**
 * warnp_syslog(enable):
 * Send future messages to syslog if ${enable} is non-zero.  Messages to
 * syslog will be truncated at WARNP_SYSLOG_MAX_LINE characters.
 */
void
warnp_syslog(int enable)
{

	/* Clean up writing to the syslog (if applicable).  */
	if (use_syslog && !enable)
		closelog();

	use_syslog = enable;
}

/**
 * warnp_syslog_priority(priority):
 * Tag future syslog messages with priority ${priority}.  Do not enable
 * syslog messages; for that, use warnp_syslog().
 */
void
warnp_syslog_priority(int priority)
{

	syslog_priority = priority;
}
