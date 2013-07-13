#ifndef _NETWORK_INTERNAL_H_
#define _NETWORK_INTERNAL_H_

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

/**
 * network_register_suspend(op):
 * Suspend ${op} operations, on all file descriptors.
 */
int network_register_suspend(int);

/**
 * network_register_resume(op):
 * Resume pending ${op} operations, on all file descriptors.
 */
int network_register_resume(int);

/**
 * network_register_fini(void):
 * Free resources allocated.
 */
void network_register_fini(void);

/**
 * network_sleep_fini(void):
 * Free resources allocated.
 */
void network_sleep_fini(void);

/**
 * network_bwlimit_get(op, len):
 * Get the amount of instantaneously allowed bandwidth for ${op} operations.
 */
int network_bwlimit_get(int, size_t *);

/**
 * network_bwlimit_eat(op, len):
 * Consume ${len} bytes of bandwidth quota for ${op} operations.
 */
int network_bwlimit_eat(int, size_t);

#endif /* !_NETWORK_H_ */
