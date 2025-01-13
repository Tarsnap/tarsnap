#ifndef SOCK_UTIL_H_
#define SOCK_UTIL_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque address structure. */
struct sock_addr;

/**
 * sock_addr_cmp(sa1, sa2):
 * Return non-zero iff the socket addresses ${sa1} and ${sa2} are different.
 */
int sock_addr_cmp(const struct sock_addr *, const struct sock_addr *);

/**
 * sock_addr_dup(sa):
 * Duplicate the provided socket address.
 */
struct sock_addr * sock_addr_dup(const struct sock_addr *);

/**
 * sock_addr_duplist(sas):
 * Duplicate the provided list of socket addresses.
 */
struct sock_addr ** sock_addr_duplist(struct sock_addr * const *);

/**
 * sock_addr_serialize(sa, buf, buflen):
 * Allocate a buffer and serialize the socket address ${sa} into it.  Return
 * the buffer via ${buf} and its length via ${buflen}.  The serialization is
 * machine and operating system dependent.
 */
int sock_addr_serialize(const struct sock_addr *, uint8_t **, size_t *);

/**
 * sock_addr_deserialize(buf, buflen):
 * Deserialize the ${buflen}-byte serialized socket address from ${buf}.
 */
struct sock_addr * sock_addr_deserialize(const uint8_t *, size_t);

/**
 * sock_addr_prettyprint(sa):
 * Allocate and return a string in one of the forms
 * /path/to/unix/socket
 * [ip.v4.ad.dr]:port
 * [ipv6:add::ress]:port
 * representing the provided socket address.
 */
char * sock_addr_prettyprint(const struct sock_addr *);

/**
 * sock_addr_ensure_port(addr):
 * Allocate a new string to serve as the address for sock_resolve().
 * If ${addr} contains a port number or is the address of a Unix domain
 * socket, duplicate that string; if not, add a port number of ":0".
 */
char * sock_addr_ensure_port(const char *);

/**
 * sock_addr_validate(addr):
 * Check that ${addr} is syntactically valid, but do not perform any address
 * resolution.
 */
int sock_addr_validate(const char *);

#endif /* !SOCK_UTIL_H_ */
