#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "netproto_internal.h"
#include "sock.h"
#include "sock_util.h"
#include "network.h"
#include "warnp.h"

#include "netproto.h"

struct netproto_connect_cookie {
	struct sock_addr ** sas;
	char * useragent;
	network_callback * callback;
	void * cookie;
	void * connect_cookie;
	NETPROTO_CONNECTION * NC;
};

static int
callback_connect(void * cookie, int s)
{
	struct netproto_connect_cookie * C = cookie;
	int rc;

	/* The connect is no longer pending. */
	C->connect_cookie = NULL;

	/* Did the connection attempt fail? */
	if (s == -1) {
		/*
		 * Call the upstream callback.  Upon being informed that the
		 * connect has failed, the upstream code is responsible for
		 * calling netproto_close, which (since we haven't called
		 * netproto_setfd yet) will call into callback_cancel and let
		 * us clean up.
		 */
		return ((C->callback)(C->cookie, NETWORK_STATUS_CONNERR));
	}

	/* Inform the netproto code that we have a socket. */
	if (netproto_setfd(C->NC, s))
		goto err2;

	/* Perform key exchange. */
	if (netproto_keyexchange(C->NC, C->useragent, C->callback, C->cookie))
		goto err1;

	/* Free the cookie. */
	sock_addr_freelist(C->sas);
	free(C->useragent);
	free(C);

	/* Success! */
	return (0);

err2:
	/* Drop the socket since we can't use it properly. */
	close(s);
err1:
	/* Call the upstream callback. */
	rc = (C->callback)(C->cookie, NETWORK_STATUS_ERR);

	/*
	 * We've called netproto_setfd, so callback_cancel won't happen; we
	 * are responsible for cleaning up after ourselves.
	 */
	sock_addr_freelist(C->sas);
	free(C->useragent);
	free(C);

	/* Return value from user callback. */
	return (rc);
}

static int
callback_cancel(void * cookie)
{
	struct netproto_connect_cookie * C = cookie;
	int rc;

	/* Cancel the connection attempt if still pending. */
	if (C->connect_cookie != NULL)
		network_connect_cancel(C->connect_cookie);

	/* We were cancelled. */
	rc = (C->callback)(C->cookie, NETWORK_STATUS_CANCEL);

	/* Free our cookie. */
	sock_addr_freelist(C->sas);
	free(C->useragent);
	free(C);

	/* Return value from user callback. */
	return (rc);
}

static struct sock_addr **
getserveraddr(void)
{
	static struct sock_addr ** srv_addr = NULL;
	static time_t srv_time = (time_t)(-1);
	struct sock_addr ** tmp_addr;
	time_t tmp_time;

	/*
	 * If we haven't done a DNS lookup already, or our cached value is
	 * more than 60 seconds old, do a DNS lookup.
	 */
	tmp_time = time(NULL);
	if ((srv_time == (time_t)(-1)) || (tmp_time > srv_time + 60)) {
		tmp_addr = sock_resolve(TSSERVER "-server.tarsnap.com:9279");
		if (tmp_addr == NULL) {
			if (srv_addr != NULL)
				warn0("Using cached DNS lookup");
			else
				warn0("Cannot obtain server address");
		}
	} else {
		tmp_addr = NULL;
	}

	/* If we have a new lookup, update the cache. */
	if (tmp_addr != NULL) {
		sock_addr_freelist(srv_addr);
		srv_addr = tmp_addr;
		srv_time = tmp_time;
	}

	/* Return a duplicate of the cached value. */
	return (sock_addr_duplist(srv_addr));
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
	struct timeval timeo;

	/* Create a cookie to be passed to callback_connect. */
	if ((C = malloc(sizeof(struct netproto_connect_cookie))) == NULL)
		goto err0;
	if ((C->useragent = strdup(useragent)) == NULL)
		goto err1;
	C->callback = callback;
	C->cookie = cookie;

	/* Look up the server's IP address. */
	if ((C->sas = getserveraddr())== NULL)
		goto err2;

	/* Try to connect to server, waiting up to 5 seconds per address. */
	timeo.tv_sec = 5;
	timeo.tv_usec = 0;
	if ((C->connect_cookie = network_connect_timeo(C->sas, &timeo,
	    callback_connect, C)) == NULL) {
		netproto_printerr(NETWORK_STATUS_CONNERR);
		goto err3;
	}

	/* Create a network protocol connection cookie. */
	if ((C->NC = netproto_alloc(callback_cancel, C)) == NULL)
		goto err4;

	/* Success! */
	return (C->NC);

err4:
	network_connect_cancel(C->connect_cookie);
err3:
	sock_addr_freelist(C->sas);
err2:
	free(C->useragent);
err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}
