#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "sock.h"
#include "warnp.h"

#include "sock_internal.h"
#include "sock_util.h"

#ifdef POSIXFAIL_INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifdef POSIXFAIL_INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

/**
 * sock_addr_cmp(sa1, sa2):
 * Return non-zero iff the socket addresses ${sa1} and ${sa2} are different.
 */
int
sock_addr_cmp(const struct sock_addr * sa1, const struct sock_addr * sa2)
{

	/* Family, socket type, and name length must match. */
	if ((sa1->ai_family != sa2->ai_family) ||
	    (sa1->ai_socktype != sa2->ai_socktype) ||
	    (sa1->namelen != sa2->namelen))
		return (1);

	/* The required length of the sockaddr must match. */
	if (memcmp(sa1->name, sa2->name, sa1->namelen) != 0)
		return (1);

	/* Everything matched. */
	return (0);
}

/**
 * sock_addr_dup(sa):
 * Duplicate the provided socket address.
 */
struct sock_addr *
sock_addr_dup(const struct sock_addr * sa)
{
	struct sock_addr * sa2;

	/* Allocate a struct sock_addr and copy fields. */
	if ((sa2 = malloc(sizeof(struct sock_addr))) == NULL)
		goto err0;
	sa2->ai_family = sa->ai_family;
	sa2->ai_socktype = sa->ai_socktype;
	sa2->namelen = sa->namelen;

	/* Allocate and copy the sockaddr. */
	if ((sa2->name = malloc(sa2->namelen)) == NULL)
		goto err1;
	memcpy(sa2->name, sa->name, sa2->namelen);

	/* Success! */
	return (sa2);

err1:
	free(sa2);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * sock_addr_duplist(sas):
 * Duplicate the provided list of socket addresses.
 */
struct sock_addr **
sock_addr_duplist(struct sock_addr * const * sas)
{
	struct sock_addr ** sas2;
	size_t i;

	/* Count socket addresses. */
	for (i = 0; sas[i] != NULL; i++)
		continue;

	/* Allocate the list to hold addresses plus a NULL terminator. */
	if ((sas2 = malloc((i + 1) * sizeof(struct sock_addr *))) == NULL)
		goto err0;

	/* Duplicate addresses and NULL-terminate. */
	for (i = 0; sas[i] != NULL; i++) {
		if ((sas2[i] = sock_addr_dup(sas[i])) == NULL)
			goto err1;
	}
	sas2[i] = NULL;

	/* Success! */
	return (sas2);

err1:
	/*
	 * Regardless of how many addresses we managed to duplicate before
	 * failing and being sent here, we have a valid socket address list,
	 * since the erroring sock_addr_dup call NULL-terminated it for us;
	 * so we can free it and its constituent addresses easily.
	 */
	sock_addr_freelist(sas2);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * sock_addr_serialize(sa, buf, buflen):
 * Allocate a buffer and serialize the socket address ${sa} into it.  Return
 * the buffer via ${buf} and its length via ${buflen}.  The serialization is
 * machine and operating system dependent.
 */
int
sock_addr_serialize(const struct sock_addr * sa,
    uint8_t ** buf, size_t * buflen)
{
	uint8_t * p;

	/* Compute buffer length and allocate buffer. */
	*buflen = 2 * sizeof(int) + sizeof(socklen_t) + sa->namelen;
	if ((p = *buf = malloc(*buflen)) == NULL)
		goto err0;

	/* Copy in data. */
	memcpy(p, &sa->ai_family, sizeof(int));
	p += sizeof(int);
	memcpy(p, &sa->ai_socktype, sizeof(int));
	p += sizeof(int);
	memcpy(p, &sa->namelen, sizeof(socklen_t));
	p += sizeof(socklen_t);
	memcpy(p, sa->name, sa->namelen);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * sock_addr_deserialize(buf, buflen):
 * Deserialize the ${buflen}-byte serialized socket address from ${buf}.
 */
struct sock_addr *
sock_addr_deserialize(const uint8_t * buf, size_t buflen)
{
	struct sock_addr * sa;

	/* Sanity check. */
	if (buflen < 2 * sizeof(int) + sizeof(socklen_t))
		goto err0;

	/* Allocate a structure and copy in fields. */
	if ((sa = malloc(sizeof(struct sock_addr))) == NULL)
		goto err0;
	memcpy(&sa->ai_family, buf, sizeof(int));
	buf += sizeof(int);
	memcpy(&sa->ai_socktype, buf, sizeof(int));
	buf += sizeof(int);
	memcpy(&sa->namelen, buf, sizeof(socklen_t));
	buf += sizeof(socklen_t);

	/* Allocate and copy the sockaddr. */
	if (buflen != 2 * sizeof(int) + sizeof(socklen_t) + sa->namelen)
		goto err1;
	if ((sa->name = malloc(sa->namelen)) == NULL)
		goto err1;
	memcpy(sa->name, buf, sa->namelen);

	/* Success! */
	return (sa);

err1:
	free(sa);
err0:
	/* Failure! */
	return (NULL);
}

/* Prettyprint an IPv4 address. */
static char *
prettyprint_ipv4(struct sockaddr * name, size_t namelen)
{
	struct sockaddr_in sa_in;
	char addr[INET_ADDRSTRLEN];
	char * s;

	/* Check name length. */
	if (namelen != sizeof(struct sockaddr_in))
		return (NULL);

	/* Copy into buffer for alignment. */
	memcpy(&sa_in, name, namelen);

	/* Convert IP address to string. */
	if (inet_ntop(AF_INET, &sa_in.sin_addr, addr, sizeof(addr)) == NULL)
		return (NULL);

	/* Construct address string. */
	if (asprintf(&s, "[%s]:%d", addr, ntohs(sa_in.sin_port)) == -1)
		return (NULL);

	/* Success! */
	return (s);
}

/* Prettyprint an IPv6 address. */
static char *
prettyprint_ipv6(struct sockaddr * name, size_t namelen)
{
	struct sockaddr_in6 sa_in6;
	char addr[INET6_ADDRSTRLEN];
	char * s;

	/* Check name length. */
	if (namelen != sizeof(struct sockaddr_in6))
		return (NULL);

	/* Copy into buffer for alignment. */
	memcpy(&sa_in6, name, namelen);

	/* Convert IPv6 address to string. */
	if (inet_ntop(AF_INET6, &sa_in6.sin6_addr, addr, sizeof(addr)) == NULL)
		return (NULL);

	/* Construct address string. */
	if (asprintf(&s, "[%s]:%d", addr, ntohs(sa_in6.sin6_port)) == -1)
		return (NULL);

	/* Success! */
	return (s);
}

/* Prettyprint a UNIX address. */
static char *
prettyprint_unix(struct sockaddr_un * name)
{

	/* Just strdup the path. */
	return (strdup(name->sun_path));
}

/**
 * sock_addr_prettyprint(sa):
 * Allocate and return a string in one of the forms
 * /path/to/unix/socket
 * [ip.v4.ad.dr]:port
 * [ipv6:add::ress]:port
 * representing the provided socket address.
 */
char *
sock_addr_prettyprint(const struct sock_addr * sa)
{

	/* Handle different types of addresses differently. */
	switch (sa->ai_family) {
	case AF_INET:
		return (prettyprint_ipv4(sa->name, sa->namelen));
	case AF_INET6:
		return (prettyprint_ipv6(sa->name, sa->namelen));
	case AF_UNIX:
		return (prettyprint_unix((struct sockaddr_un *)(sa->name)));
	default:
		return (strdup("Unknown address"));
	}
}

/**
 * sock_addr_ensure_port(addr):
 * Allocate a new string to serve as the address for sock_resolve().
 * If ${addr} contains a port number or is the address of a Unix domain
 * socket, duplicate that string; if not, add a port number of ":0".
 */
char *
sock_addr_ensure_port(const char * addr)
{
	char * bind_addr;
	char * cr;

	/* Sanity check. */
	assert(addr != NULL);

	/* Where is the right-most colon in $addr? */
	cr = strrchr(addr, ':');

	/* Figure out what type of address $addr is. */
	if (cr == addr) {
		/*
		 * If the right-most colon is the first char, it's not a valid
		 * address, but we'll strdup it anyway.
		 */
		bind_addr = strdup(addr);
	} else if (addr[0] == '/') {
		/* It's a Unix domain socket and doesn't need a port number. */
		bind_addr = strdup(addr);
	} else if (addr[0] != '[') {
		/* It's a hostname... */
		if (cr == NULL) {
			/* ... without any port number, so we add ":0". */
			if (asprintf(&bind_addr, "%s:0", addr) == -1)
				goto err0;
		} else {
			/* ... which already has a port number. */
			bind_addr = strdup(addr);
		}
	} else {
		/* addr[0] == '[', so it's an address... */
		if ((cr == NULL) || (cr[-1] != ']')) {
			/* ... without a port number, so we add ":0". */
			if (asprintf(&bind_addr, "%s:0", addr) == -1)
				goto err0;
		} else {
			/* ... which already has a port number. */
			bind_addr = strdup(addr);
		}
	}

	/* Success! */
	return (bind_addr);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * sock_addr_validate(addr):
 * Check that ${addr} is syntactically valid, but do not perform any address
 * resolution.
 */
int
sock_addr_validate(const char * addr)
{

	/* Sanity check. */
	assert(addr != NULL);

	/* Check for an empty address. */
	if (strlen(addr) == 0) {
		warn0("Empty socket address.");
		goto err0;
	}

	/* If this isn't a UNIX socket address, check for a missing hostname. */
	if ((addr[0] != '/') && (addr[0] == ':')) {
		warn0("No host in \"%s\"", addr);
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
