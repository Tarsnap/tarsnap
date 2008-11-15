#ifndef _NETWORK_INTERNAL_H_
#define _NETWORK_INTERNAL_H_

/* Maximum number of bytes which can be read/writ right now. */
extern size_t network_bwlimit_read;
extern size_t network_bwlimit_write;

/* Macros for handling struct timeval values. */
#define tv_lt(a, b)				\
	(((a)->tv_sec < (b)->tv_sec) ||		\
	    (((a)->tv_sec == (b)->tv_sec) &&	\
		((a)->tv_usec < (b)->tv_usec)))
#define tv_add(a, b)	do {			\
	(a)->tv_sec += (b)->tv_sec;		\
	(a)->tv_usec += (b)->tv_usec;		\
	if ((a)->tv_usec >= 1000000) {		\
		(a)->tv_usec -= 1000000;	\
		(a)->tv_sec += 1;		\
	}					\
} while (0)
#define tv_sub(a, b)	do {			\
	(a)->tv_sec -= (b)->tv_sec;		\
	(a)->tv_usec -= (b)->tv_usec;		\
	if ((a)->tv_usec < 0) {			\
		(a)->tv_usec += 1000000;	\
		(a)->tv_sec -= 1;		\
	}					\
} while (0)

/**
 * network_cork(fd):
 * Clear the TCP_NODELAY socket option, and set TCP_CORK or TCP_NOPUSH if
 * either is defined.
 */
int network_cork(int);

/**
 * network_uncork(fd):
 * Set the TCP_NODELAY socket option, and clear TCP_CORK or TCP_NOPUSH if
 * either is defined.
 */
int network_uncork(int);

#endif /* !_NETWORK_H_ */
