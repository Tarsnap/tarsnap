#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "netproto_internal.h"
#include "network.h"
#include "warnp.h"

#include "netproto.h"

struct netproto_connect_cookie {
	char * useragent;
	network_callback * callback;
	void * cookie;
	NETPROTO_CONNECTION * NC;
};

static network_callback callback_connect;
static int dnslookup(const char *, struct sockaddr **, socklen_t *);

static int
callback_connect(void * cookie, int status)
{
	struct netproto_connect_cookie * C = cookie;
	int rc;

	/* Did the connection attempt fail? */
	if (status != NETWORK_STATUS_OK)
		goto failed;

	/* Perform key exchange. */
	if (netproto_keyexchange(C->NC, C->useragent,
	    C->callback, C->cookie)) {
		status = NETWORK_STATUS_ERR;
		goto failed;
	}

	/* Free the cookie. */
	free(C->useragent);
	free(C);

	/* Success! */
	return (0);

failed:
	/* Call the upstream callback. */
	rc = (C->callback)(C->cookie, status);

	/* Free the cookie. */
	free(C->useragent);
	free(C);

	/* Return value from user callback. */
	return (rc);
}

#ifdef HAVE_GETADDRINFO
static int
dnslookup(const char * sname, struct sockaddr ** addr, socklen_t * addrlen)
{
	struct addrinfo hints;
	struct addrinfo * res;
	int error;

	/* Do the DNS lookup. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
#ifdef AI_NUMERICSERV
	/**
	 * XXX Portability
	 * XXX POSIX states that AI_NUMERICSERV should be defined in netdb.h,
	 * XXX but some operating systems don't provide it.  The flag isn't
	 * XXX really necessary anyway, since getaddrinfo should interpret
	 * XXX "9279" as 'a string specifying a decimal port number'; but
	 * XXX passing the flag (on operating systems which provide it) might
	 * XXX avoid waiting for a name resolution service (e.g., NIS+) to
	 * XXX be invoked.
	 */
	hints.ai_flags = AI_NUMERICSERV;
#endif
	if ((error = getaddrinfo(sname, "9279",
	    &hints, &res)) != 0) {
		warn0("Error looking up %s: %s", sname, gai_strerror(error));
		goto err0;
	}
	if (res == NULL) {
		warn0("Cannot look up %s", sname);
		goto err0;
	}

	/* Create a copy of the structure. */
	if ((*addr = malloc(res->ai_addrlen)) == NULL)
		goto err0;
	memcpy(*addr, res->ai_addr, res->ai_addrlen);
	*addrlen = res->ai_addrlen;

	/* Free the returned list of addresses. */
	freeaddrinfo(res);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
#else
static int
dnslookup(const char * sname, struct sockaddr ** addr, socklen_t * addrlen)
{
	struct hostent * hp;
	struct sockaddr_in sin;

	if ((hp = gethostbyname(sname)) == NULL) {
		warn0("Error looking up %s", sname);
		goto err0;
	}

	/* Construct a struct sockaddr_in. */
	bzero(&sin, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(9279);
	memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);

	/* Make a copy of this to return. */
	if ((*addr = malloc(sizeof(struct sockaddr_in))) == NULL)
		goto err0;
	memcpy(*addr, &sin, sizeof(struct sockaddr_in));
	*addrlen = sizeof(struct sockaddr_in);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
#endif

static void
getserveraddr(struct sockaddr ** addr, socklen_t * addrlen)
{
	static struct sockaddr * srv_addr = NULL;
	static socklen_t srv_addrlen = 0;
	static time_t srv_time = (time_t)(-1);
	struct sockaddr * tmp_addr;
	socklen_t tmp_addrlen;
	time_t tmp_time;

	/*
	 * If we haven't done a DNS lookup already, or our cached value is
	 * more than 60 seconds old, do a DNS lookup.
	 */
	tmp_time = time(NULL);
	if ((srv_time == (time_t)(-1)) || (tmp_time > srv_time + 60)) {
		if (dnslookup(TSSERVER "-server.tarsnap.com",
		    &tmp_addr, &tmp_addrlen)) {
			if (srv_addr != NULL)
				warn0("Using cached DNS lookup");
			else
				warn0("Cannot obtain server address");
			tmp_addr = NULL;
		}
	} else {
		tmp_addr = NULL;
	}

	/* If we have a new lookup, update the cache. */
	if (tmp_addr != NULL) {
		free(srv_addr);
		srv_addr = tmp_addr;
		srv_addrlen = tmp_addrlen;
		srv_time = tmp_time;
	}

	/* Return the cached value. */
	*addr = srv_addr;
	*addrlen = srv_addrlen;
}

/**
 * netproto_connect(useragent, callback, cookie):
 * Create a socket, connect to the tarsnap server, and perform the necessary
 * key exchange.  Return a network protocol connection cookie; note that
 * this cookie must not be used until the callback is called.
 */
NETPROTO_CONNECTION *
netproto_connect(const char * useragent,
    network_callback * callback, void * cookie)
{
	struct netproto_connect_cookie * C;
	int s;
	NETPROTO_CONNECTION * NC;
	struct sockaddr * addr;
	socklen_t addrlen;
	struct timeval timeo;

	/* Create a network socket. */
	if ((s = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		warnp("socket()");
		goto err0;
	}

	/* Wrap the socket into a protocol-layer cookie. */
	if ((NC = netproto_open(s)) == NULL) {
		close(s);
		goto err0;
	}

	/* Create a cookie to be passed to callback_connect. */
	if ((C = malloc(sizeof(struct netproto_connect_cookie))) == NULL)
		goto err1;
	if ((C->useragent = strdup(useragent)) == NULL)
		goto err2;
	C->callback = callback;
	C->cookie = cookie;
	C->NC = NC;

	/* Look up the server's IP address. */
	getserveraddr(&addr, &addrlen);
	if (addr == NULL)
		goto err3;

	/* Try to connect to server within 5 seconds. */
	timeo.tv_sec = 5;
	timeo.tv_usec = 0;
	if (network_connect(s, addr, addrlen,
	    &timeo, callback_connect, C)) {
		netproto_printerr(NETWORK_STATUS_CONNERR);
		goto err3;
	}

	/* Success! */
	return (NC);

err3:
	free(C->useragent);
err2:
	free(C);
err1:
	netproto_close(NC);
err0:
	/* Failure! */
	return (NULL);
}
