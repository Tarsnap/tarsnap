#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "events.h"
#include "graceful_shutdown.h"
#include "proto_conn.h"
#include "sock.h"
#include "warnp.h"
#include "pushbits.h"

struct spipe_state {
	int conndone;
	pthread_t threads[2];
};

static int
callback_conndied(void * cookie)
{
	struct spipe_state * ET = cookie;

	/* The connection is dead. */
	ET->conndone = 1;

	/* Success! */
	return (0);
}

static int
callback_graceful_shutdown(void * cookie)
{
	struct spipe_state * ET = cookie;

	/* We've been asked to shut down gracefully. */
	ET->conndone = 1;

	/* Success! */
	return (0);
}

int
main(int argc, char * argv[])
{
	const char * opt_t;
	const char * opt_k;
	int opt_j;
	int opt_o;
	int s[2];
	void * conn_cookie;
	struct spipe_state ET;
	int rc;

	WARNP_INIT;

	/* Initialize the connection state. */
	ET.conndone = 0;

	/* Parse command line. */
	if (argc != 5) {
		fprintf(stderr, "usage: spipe -t <target socket> -k <key file>\n");
		goto err0;
	}
	opt_t = NULL;
	opt_k = NULL;
	opt_j = 0;
	opt_o = 0;
	while (++opt_j < argc) {
		if (strcmp(argv[opt_j], "-t") == 0) {
			if (++opt_j >= argc) {
				warn0("Missing argument to -t");
				goto err0;
			}
			opt_t = argv[opt_j];
		} else if (strcmp(argv[opt_j], "-k") == 0) {
			if (++opt_j >= argc) {
				warn0("Missing argument to -k");
				goto err0;
			}
			opt_k = argv[opt_j];
		} else if (strcmp(argv[opt_j], "-o") == 0) {
			opt_o = 1;
		} else {
			warn0("Unrecognized option: %s", argv[opt_j]);
			goto err0;
		}
	}

	/* Sanity-check options. */
	if (opt_t == NULL) {
		warn0("Missing -t option");
		goto err0;
	}
	if (opt_k == NULL) {
		warn0("Missing -k option");
		goto err0;
	}

	/* Create a socket pair. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s)) {
		warnp("socketpair");
		goto err0;
	}

	/* Create a connection. */
	if ((conn_cookie = proto_conn_create(s[1], opt_t, opt_k,
	    opt_o, callback_conndied, &ET)) == NULL) {
		warnp("Could not create connection");
		goto err1;
	}

	/* Push bits from the socket to stdout. */
	if (pushbits(s[0], STDOUT_FILENO, &ET.threads[1])) {
		warnp("Could not push bits");
		goto err5;
	}

	/* Push bits from stdin into the socket. */
	if (pushbits(STDIN_FILENO, s[0], &ET.threads[0])) {
		warnp("Could not push bits");
		goto err6;
	}

	/* Register a handler for SIGTERM. */
	if (graceful_shutdown_initialize(&callback_graceful_shutdown, &ET)) {
		warn0("Failed to start graceful_shutdown timer");
		goto err7;
	}

	/* Loop until we're done with the connection. */
	if (events_spin(&ET.conndone)) {
		warnp("Error running event loop");
		if ((rc = pthread_cancel(ET.threads[0])) != 0)
			warn0("pthread_cancel: %s", strerror(rc));
		if ((rc = pthread_join(ET.threads[0], NULL)) != 0)
			warn0("pthread_join: %s", strerror(rc));
		if ((rc = pthread_cancel(ET.threads[1])) != 0)
			warn0("pthread_cancel: %s", strerror(rc));
		if ((rc = pthread_join(ET.threads[1], NULL)) != 0)
			warn0("pthread_join: %s", strerror(rc));
		goto err5;
	}

err7:
	pthread_cancel(ET.threads[0]);
	pthread_join(ET.threads[0], NULL);
err6:
	pthread_cancel(ET.threads[1]);
	pthread_join(ET.threads[1], NULL);
err5:
	proto_conn_drop(conn_cookie, PROTO_CONN_CANCELLED);

	/* Close the local socket. */
	close(s[0]);

err1:
	close(s[1]);

err0:
	/* Failure! */
	return (1);
}
