#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "netproto_internal.h"
#include "network.h"
#include "warnp.h"

#include "netproto.h"

struct netproto_connect_cookie {
	network_callback * callback;
	void * cookie;
	NETPROTO_CONNECTION * NC;
};

static network_callback callback_connect;

static int
callback_connect(void * cookie, int status)
{
	struct netproto_connect_cookie * C = cookie;
	int rc;

	/* Did the connection attempt fail? */
	if (status != NETWORK_STATUS_OK)
		goto failed;

	/* Perform key exchange. */
	if (netproto_keyexchange(C->NC, C->callback, C->cookie)) {
		status = NETWORK_STATUS_ERR;
		goto failed;
	}

	/* Free the cookie. */
	free(C);

	/* Success! */
	return (0);

failed:
	/* Call the upstream callback. */
	rc = (C->callback)(C->cookie, status);

	/* Free the cookie. */
	free(C);

	/* Return value from user callback. */
	return (rc);
}

/**
 * netproto_connect(callback, cookie):
 * Create a socket, connect to the tarsnap server, and perform the neccesary
 * key exchange.  Return a network protocol connection cookie; note that
 * this cookie must not be used until the callback is called.
 */
NETPROTO_CONNECTION *
netproto_connect(network_callback * callback, void * cookie)
{
	struct netproto_connect_cookie * C;
	int s;
	NETPROTO_CONNECTION * NC;
#ifdef HAVE_GETADDRINFO
	struct addrinfo hints;
	struct addrinfo * res;
	int error;
#else
	struct hostent * hp;
	struct sockaddr_in sin;
#endif
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
	C->callback = callback;
	C->cookie = cookie;
	C->NC = NC;

	/* Look up the server's IP address. */
#ifdef HAVE_GETADDRINFO
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
	if ((error = getaddrinfo(TSSERVER "-server.tarsnap.com", "9279",
	    &hints, &res)) != 0) {
		warn0("Error looking up " TSSERVER "-server.tarsnap.com: %s",
		    gai_strerror(error));
		goto err2;
	}
	if (res == NULL) {
		warn0("Cannot look up " TSSERVER "-server.tarsnap.com");
		goto err2;
	}
	addr = res->ai_addr;
	addrlen = res->ai_addrlen;
#else /* !HAVE_GETADDRINFO */
	if ((hp = gethostbyname(TSSERVER "-server.tarsnap.com")) == NULL) {
		warn0("Error looking up " TSSERVER "-server.tarsnap.com");
		goto err2;
	}
	bzero(&sin, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(9279);
	memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
	addr = (struct sockaddr *)&sin;
	addrlen = sizeof(struct sockaddr_in);
#endif /* HAVE_GETADDRINFO */


	/* Try to connect to server within 5 seconds. */
	timeo.tv_sec = 5;
	timeo.tv_usec = 0;
	if (network_connect(s, addr, addrlen,
	    &timeo, callback_connect, C)) {
		netproto_printerr(NETWORK_STATUS_CONNERR);
		goto err2;
	}

	/* Success! */
	return (NC);

err2:
	free(C);
err1:
	netproto_close(NC);
err0:
	/* Failure! */
	return (NULL);
}
