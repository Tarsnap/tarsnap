#ifndef _WARNP_H_
#define _WARNP_H_

#include <err.h>
#include <errno.h>

/*
 * If compiled with DEBUG defined, print __FILE__ and __LINE__.
 */
#ifdef DEBUG
#define warnline	do {				\
	warnx("%s, %d", __FILE__, __LINE__);		\
} while (0)
#else
#define warnline
#endif

/*
 * Call warn(3) or warnx(3) depending upon whether errno == 0; and clear
 * errno (so that the standard error message isn't repeated later).
 */
#define	warnp(format, ...) do {				\
	warnline;					\
	if (errno != 0) {				\
		warn(format, ## __VA_ARGS__);		\
		errno = 0;				\
	} else						\
		warnx(format, ## __VA_ARGS__);		\
} while (0)

/*
 * Call warnx(3) and set errno == 0.  Unlike warnp, this should be used
 * in cases where we're reporting a problem which we discover ourselves
 * rather than one which is reported to us from a library or the kernel.
 */
#define warn0(format, ...) do {				\
	warnline;					\
	warnx(format, ## __VA_ARGS__);			\
	errno = 0;					\
} while (0)

#endif /* !_WARNP_H_ */
