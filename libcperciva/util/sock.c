#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "imalloc.h"
#include "warnp.h"

#include "sock.h"
#include "sock_internal.h"

/* Convert a path into a socket address. */
static struct sock_addr **
sock_resolve_unix(const char * addr)
{
	struct sock_addr ** sas;
	struct sock_addr * sa;
	struct sockaddr_un * sa_un;

	/* Allocate and populate a sockaddr_un structure. */
	if ((sa_un = malloc(sizeof(struct sockaddr_un))) == NULL)
		goto err0;
	memset(sa_un, 0, sizeof(struct sockaddr_un));
	sa_un->sun_family = AF_UNIX;
	if (strlen(addr) >= sizeof(sa_un->sun_path)) {
		warn0("socket path too long: %s", addr);
		goto err1;
	}
	strcpy(sa_un->sun_path, addr);

	/* Allocate and populate our wrapper. */
	if ((sa = malloc(sizeof(struct sock_addr))) == NULL)
		goto err1;
	sa->ai_family = AF_UNIX;
	sa->ai_socktype = SOCK_STREAM;
	sa->name = (struct sockaddr *)sa_un;
	sa->namelen = sizeof(struct sockaddr_un);

	/* Allocate and populate an array of pointers. */
	if ((sas = malloc(2 * sizeof(struct sock_addr *))) == NULL)
		goto err2;
	sas[0] = sa;
	sas[1] = NULL;

	/* Success! */
	return (sas);

err2:
	free(sa);
err1:
	free(sa_un);
err0:
	/* Failure! */
	return (NULL);
}

/* Resolve a host into a list of socket addresses. */
static struct sock_addr **
sock_resolve_host(const char * addr, const char * ports)
{
	struct addrinfo hints;
	struct addrinfo * res;
	struct addrinfo * r;
	struct sock_addr ** sas;
	size_t n;
	int error;

	/* Create hints structure. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	/* Perform DNS lookup. */
	if ((error = getaddrinfo(addr, ports, &hints, &res)) != 0) {
		warn0("Error looking up %s: %s", addr, gai_strerror(error));
		goto err0;
	}

	/* Count addresses returned. */
	for (n = 0, r = res; r != NULL; r = r->ai_next)
		n++;

	/* Allocate our response array. */
	if (IMALLOC(sas, n + 1, struct sock_addr *))
		goto err1;

	/* Create address structures. */
	for (n = 0, r = res; r != NULL; n++, r = r->ai_next) {
		/* Allocate a structure. */
		if ((sas[n] = malloc(sizeof(struct sock_addr))) == NULL)
			goto err2;

		/* Copy in the address metadata. */
		sas[n]->ai_family = r->ai_family;
		sas[n]->ai_socktype = r->ai_socktype;
		sas[n]->namelen = r->ai_addrlen;

		/* Duplicate the address. */
		if ((sas[n]->name = malloc(sas[n]->namelen)) == NULL)
			goto err3;
		memcpy(sas[n]->name, r->ai_addr, sas[n]->namelen);
	}

	/* Terminate array with a NULL. */
	sas[n] = NULL;

	/* Free the linked list of addresses returned by getaddrinfo. */
	freeaddrinfo(res);

	/* Success! */
	return (sas);

err3:
	free(sas[n]);
err2:
	for (; n > 0; n--) {
		free(sas[n - 1]->name);
		free(sas[n - 1]);
	}
	free(sas);
err1:
	freeaddrinfo(res);
err0:
	/* Failure! */
	return (NULL);
}

/* Parse an IPv6 address into a socket address. */
static struct sock_addr **
sock_resolve_ipv6(const char * addr, in_port_t p)
{
	struct sock_addr ** sas;
	struct sock_addr * sa;
	struct sockaddr_in6 * sin6;

	/* Allocate and populate a sockaddr_in6 structure. */
	if ((sin6 = malloc(sizeof(struct sockaddr_in6))) == NULL)
		goto err0;
	memset(sin6, 0, sizeof(struct sockaddr_in6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = htons(p);
	if (inet_pton(AF_INET6, addr, &sin6->sin6_addr) != 1) {
		warn0("Error parsing IP address: %s", addr);
		goto err1;
	}

	/* Allocate and populate our wrapper. */
	if ((sa = malloc(sizeof(struct sock_addr))) == NULL)
		goto err1;
	sa->ai_family = AF_INET6;
	sa->ai_socktype = SOCK_STREAM;
	sa->name = (struct sockaddr *)sin6;
	sa->namelen = sizeof(struct sockaddr_in6);

	/* Allocate and populate an array of pointers. */
	if ((sas = malloc(2 * sizeof(struct sock_addr *))) == NULL)
		goto err2;
	sas[0] = sa;
	sas[1] = NULL;

	/* Success! */
	return (sas);

err2:
	free(sa);
err1:
	free(sin6);
err0:
	/* Failure! */
	return (NULL);
}

/* Parse an IPv4 address into a socket address. */
static struct sock_addr **
sock_resolve_ipv4(const char * addr, in_port_t p)
{
	struct sock_addr ** sas;
	struct sock_addr * sa;
	struct sockaddr_in * sin;

	/* Allocate and populate a sockaddr_in structure. */
	if ((sin = malloc(sizeof(struct sockaddr_in))) == NULL)
		goto err0;
	memset(sin, 0, sizeof(struct sockaddr_in));
	sin->sin_family = AF_INET;
	sin->sin_port = htons(p);
	if (inet_pton(AF_INET, addr, &sin->sin_addr) != 1) {
		warn0("Error parsing IP address: %s", addr);
		goto err1;
	}

	/* Allocate and populate our wrapper. */
	if ((sa = malloc(sizeof(struct sock_addr))) == NULL)
		goto err1;
	sa->ai_family = AF_INET;
	sa->ai_socktype = SOCK_STREAM;
	sa->name = (struct sockaddr *)sin;
	sa->namelen = sizeof(struct sockaddr_in);

	/* Allocate and populate an array of pointers. */
	if ((sas = malloc(2 * sizeof(struct sock_addr *))) == NULL)
		goto err2;
	sas[0] = sa;
	sas[1] = NULL;

	/* Success! */
	return (sas);

err2:
	free(sa);
err1:
	free(sin);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * sock_resolve(addr):
 * Return a NULL-terminated array of pointers to sock_addr structures.
 */
struct sock_addr **
sock_resolve(const char * addr)
{
	struct sock_addr ** res;
	char * s;
	char * ports;
	char * ips;
	long p;

	/* If the address starts with '/', it's a unix socket. */
	if (addr[0] == '/') {
		res = sock_resolve_unix(addr);
		goto done;
	}

	/* Copy the address so that we can mangle it. */
	if ((s = strdup(addr)) == NULL)
		goto err0;

	/* The address should end with :port.  Look for the last ':'. */
	if ((ports = strrchr(s, ':')) == NULL) {
		warn0("Address must contain port number: %s", s);
		goto err1;
	}
	*ports++ = '\0';

	/* If the address doesn't start with '[', it's a host name. */
	if (s[0] != '[') {
		res = sock_resolve_host(s, ports);
		goto done1;
	}

	/* The address (sans :port) should end with ']'. */
	if (s[strlen(s) - 1] != ']') {
		warn0("Invalid [IP address]: %s", s);
		goto err1;
	}

	/* Extract the IP address string. */
	ips = &s[1];
	ips[strlen(ips) - 1] = '\0';

	/*
	 * Parse the port number.  If strtol fails to parse the port number,
	 * it will return 0; but that's fine since port 0 is invalid anyway.
	 */
	p = strtol(ports, NULL, 10);
	if ((p <= 0) || (p >= 65536)) {
		warn0("Invalid port number: %s", ports);
		goto err1;
	}

	/* If the IP address contains ':', it's IPv6; otherwise, IPv4. */
	if (strrchr(ips, ':') != NULL)
		res = sock_resolve_ipv6(ips, p);
	else
		res = sock_resolve_ipv4(ips, p);

done1:
	/* Free string allocated by strdup. */
	free(s);
done:
	/* Return result from sock_resolve_foo. */
	return (res);

err1:
	free(s);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * sock_listener(sa):
 * Create a socket, set SO_REUSEADDR, bind it to the socket address ${sa},
 * mark it for listening, and mark it as non-blocking.
 */
int
sock_listener(const struct sock_addr * sa)
{
	int s;
	int val = 1;

	/* Create a socket. */
	if ((s = socket(sa->ai_family, sa->ai_socktype, 0)) == -1) {
		warnp("socket(%d, %d)", sa->ai_family, sa->ai_socktype);
		goto err0;
	}

	/* Set SO_REUSEADDR. */
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) {
		warnp("setsockopt(SO_REUSEADDR)");
		goto err1;
	}

	/* Bind the socket. */
	if (bind(s, sa->name, sa->namelen)) {
		warnp("error binding socket");
		goto err1;
	}

	/* Mark the socket as listening. */
	if (listen(s, 10)) {
		warnp("error marking socket as listening");
		goto err1;
	}

	/* Make the socket as non-blocking. */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
		warnp("error marking socket as non-blocking");
		goto err1;
	}

	/* Success! */
	return (s);

err1:
	close(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * sock_connect(sas):
 * Iterate through the addresses in ${sas}, attempting to create a socket and
 * connect (blockingly).  Once connected, stop iterating, mark the socket as
 * non-blocking, and return it.
 */
int
sock_connect(struct sock_addr * const * sas)
{
	int s = -1;

	/* Iterate through the addresses provided. */
	for (; sas[0] != NULL; sas++) {
		/* Create a socket. */
		if ((s = socket(sas[0]->ai_family,
		    sas[0]->ai_socktype, 0)) == -1)
			continue;

		/* Attempt to connect. */
		if (connect(s, sas[0]->name, sas[0]->namelen) == 0)
			break;

		/* Close the socket; this address didn't work. */
		close(s);
	}

	/* Did we manage to connect? */
	if (sas[0] == NULL) {
		warn0("Could not connect");
		goto err0;
	}

	/* Mark the socket as non-blocking. */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make connection non-blocking");
		goto err1;
	}

	/* Success! */
	return (s);

err1:
	close(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * sock_connect_nb(sa):
 * Create a socket, mark it as non-blocking, and attempt to connect to the
 * address ${sa}.  Return the socket (connected or in the process of
 * connecting) or -1 on error.
 */
int
sock_connect_nb(const struct sock_addr * sa)
{
	int s;

	/* Create a socket. */
	if ((s = socket(sa->ai_family, sa->ai_socktype, 0)) == -1)
		goto err0;

	/* Mark the socket as non-blocking. */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make socket non-blocking");
		goto err1;
	}

	/* Attempt to connect. */
	if ((connect(s, sa->name, sa->namelen) == -1) &&
	    (errno != EINPROGRESS) &&
	    (errno == EINTR))
		goto err1;

	/* We have a connect(ed|ing) socket. */
	return (s);

err1:
	close(s);
err0:
	/* We failed to connect to this address. */
	return (-1);
}

/**
 * sock_addr_free(sa):
 * Free the provided sock_addr structure.
 */
void
sock_addr_free(struct sock_addr * sa)
{

	/* Compatibility with free(NULL). */
	if (sa == NULL)
		return;

	/* Free the protocol-specific address structure and our struct. */
	free(sa->name);
	free(sa);
}

/**
 * sock_addr_freelist(sas):
 * Free the provided NULL-terminated array of sock_addr structures.
 */
void
sock_addr_freelist(struct sock_addr ** sas)
{
	struct sock_addr ** p;

	/* Compatibility with free(NULL). */
	if (sas == NULL)
		return;

	/* Free structures until we hit NULL. */
	for (p = sas; *p != NULL; p++)
		sock_addr_free(*p);

	/* Free the list. */
	free(sas);
}
