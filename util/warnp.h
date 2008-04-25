#ifndef _WARNP_H_
#define _WARNP_H_

#include <err.h>
#include <errno.h>

/*
 * Call warn(3) or warnx(3) depending upon whether errno == 0; and clear
 * errno (so that the standard error message isn't repeated later).  If
 * compiled with DEBUG defined, print __FILE__ and __LINE__ as well.
 */
#ifdef DEBUG
#define	warnp(format, ...) do {				\
	warnx("%s, %d", __FILE__, __LINE__);		\
	if (errno != 0) {				\
		warn(format, ## __VA_ARGS__);		\
		errno = 0;				\
	} else						\
		warnx(format, ## __VA_ARGS__);		\
} while (0)
#else
#define	warnp(format, ...) do {				\
	if (errno != 0) {				\
		warn(format, ## __VA_ARGS__);		\
		errno = 0;				\
	} else						\
		warnx(format, ## __VA_ARGS__);		\
} while (0)
#endif

/*
 * Call warnx(3) and set errno == 0.  Unlike warnp, this should be used
 * in cases where we're reporting a problem which we discover ourselves
 * rather than one which is reported to us from a library or the kernel.
 */
#ifdef DEBUG
#define warn0(format, ...) do {				\
	warnx("%s, %d", __FILE__, __LINE__);		\
	warnx(format, ## __VA_ARGS__);			\
	errno = 0;					\
} while (0)
#else
#define	warn0(format, ...) do {				\
	warnx(format, ## __VA_ARGS__);			\
	errno = 0;					\
} while (0)
#endif

#endif /* !_WARNP_H_ */
