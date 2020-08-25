#ifndef _WARNP_H_
#define _WARNP_H_

#include <errno.h>
#include <stddef.h>

/* Avoid namespace collisions with BSD <err.h>. */
#define warn libcperciva_warn
#define warnx libcperciva_warnx

/*
 * Maximum length of messages sent to syslog; longer warnings will
 * be truncated.
 */
#define WARNP_SYSLOG_MAX_LINE 4095

/**
 * warnp_setprogname(progname):
 * Set the program name to be used by warn() and warnx() to ${progname}.
 */
void warnp_setprogname(const char *);
#define WARNP_INIT	do {		\
	if (argv[0] != NULL)		\
		warnp_setprogname(argv[0]);	\
} while (0)

/**
 * warnp_syslog(enable):
 * Send future messages to syslog if ${enable} is non-zero.  Messages to
 * syslog will be truncated at WARNP_SYSLOG_MAX_LINE characters.
 */
void warnp_syslog(int);

/**
 * warnp_syslog_priority(priority):
 * Tag future syslog messages with priority ${priority}.  Do not enable
 * syslog messages; for that, use warnp_syslog().
 */
void warnp_syslog_priority(int);

/* As in BSD <err.h>. */
void warn(const char *, ...);
void warnx(const char *, ...);

/*
 * If compiled with DEBUG defined, print __FILE__ and __LINE__.
 */
#ifdef DEBUG
#define warnline	do {				\
	warnx("%s, %d", __FILE__, __LINE__);	\
} while (0)
#else
#define warnline
#endif

/*
 * Call warn(3) or warnx(3) depending upon whether errno == 0; and clear
 * errno (so that the standard error message isn't repeated later).
 */
#define	warnp(...) do {					\
	warnline;					\
	if (errno != 0) {				\
		warn(__VA_ARGS__);		\
		errno = 0;				\
	} else						\
		warnx(__VA_ARGS__);		\
} while (0)

/*
 * Call warnx(3) and set errno == 0.  Unlike warnp(), this should be used
 * in cases where we're reporting a problem which we discover ourselves
 * rather than one which is reported to us from a library or the kernel.
 */
#define warn0(...) do {					\
	warnline;					\
	warnx(__VA_ARGS__);			\
	errno = 0;					\
} while (0)

#endif /* !_WARNP_H_ */
