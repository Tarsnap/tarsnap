#ifndef _NETWORK_CORK_H_
#define _NETWORK_CORK_H_

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

#endif /* !_NETWORK_CORK_H_ */
