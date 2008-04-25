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
	struct addrinfo hints;
	struct addrinfo * res;
	int error;
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
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->NC = NC;

	/* Look up the server's IP address. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICSERV;
	if ((error = getaddrinfo(TSSERVER "-server.tarsnap.com", "9279",
	    &hints, &res)) != 0) {
		warn0("Error looking up " TSSERVER "-server.tarsnap.com: %s",
		    gai_strerror(error));
		goto err1;
	}
	if (res == NULL) {
		warn0("Cannot look up " TSSERVER "-server.tarsnap.com");
		goto err1;
	}

	/* Try to connect to server within 5 seconds. */
	timeo.tv_sec = 5;
	timeo.tv_usec = 0;
	if (network_connect(s, res->ai_addr, res->ai_addrlen,
	    &timeo, callback_connect, C)) {
		netproto_printerr(NETWORK_STATUS_CONNERR);
		goto err1;
	}

	/* Success! */
	return (NC);

err1:
	netproto_close(NC);
err0:
	/* Failure! */
	return (NULL);
}
