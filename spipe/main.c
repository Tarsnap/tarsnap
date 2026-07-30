/*-
 * Copyright 2005-2014 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "daemonize.h"
#include "events.h"
#include "getopt.h"
#include "parsenum.h"
#include "proto_crypt.h"
#include "sock.h"
#include "sock_util.h"
#include "warnp.h"

struct accept_state {
	int s;
	const struct sock_addr * sas;
	const char * tgt;
	struct proto_secret * K;
	int nofps;
	int nokeepalive;
	double timeo;
	pthread_t * threads;
	int stopped;
};

static void
usage(void)
{

	fprintf(stderr, "usage: spipe -t <target socket> -k <key file>"
	    " [-DFj] [-f | -g] [-n <max # connections>]\n"
	    "    [-o <connection timeout>] [-p <pidfile>] <source socket>\n");
	fprintf(stderr, "       spipe --version\n");
	exit(1);
}

/* Simplify error-handling in command-line parse loop. */
#define OPT_EPARSE(opt, arg) do {					\
	warn0("Error parsing argument: %s %s", opt, arg);		\
	exit(1);							\
} while (0)

static void *
receive_thread(void * cookie)
{
	struct accept_state * A = cookie;

	if (proto_crypt_pipe(A->s, A->sas, A->tgt, A->K, A->nofps,
	    A->nokeepalive, A->timeo))
		return (NULL);

	return (cookie);
}

static void *
accept_thread(void * cookie)
{
	struct accept_state * A = cookie;

	if (proto_crypt_listener(A->s, A->sas, A->tgt, A->K, A->nofps,
	    A->nokeepalive, A->timeo))
		return (NULL);

	return (cookie);
}

static void
signal_handler(int sig)
{

	(void)sig; /* UNUSED */

	/* We just need to interrupt blocking system calls. */
}

int
main(int argc, char * argv[])
{
	const char * tgt = NULL;
	const char * opt_k = NULL;
	const char * opt_p = NULL;
	int opt_D = 0;
	int opt_F = 0;
	int opt_f = 0;
	int opt_g = 0;
	int opt_j = 0;
	int64_t opt_n = 0;
	double opt_o = 0.0;
	int nofps = 0;
	int nokeepalive = 0;
	const char * ch;
	struct sock_addr ** sas_s;
	struct sock_addr ** sas_t;
	struct sock_addr * sa_b;
	struct proto_secret * K;
	int s[2];
	struct accept_state ET;
	int rc;
	struct sigaction sa;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPT("-D"):
			if (opt_D)
				usage();
			opt_D = 1;
			break;
		GETOPT_OPT("-F"):
			if (opt_F)
				usage();
			opt_F = 1;
			break;
		GETOPT_OPT("-f"):
			if (opt_f || opt_g)
				usage();
			opt_f = 1;
			break;
		GETOPT_OPT("-g"):
			if (opt_f || opt_g)
				usage();
			opt_g = 1;
			break;
		GETOPT_OPT("-j"):
			if (opt_j)
				usage();
			opt_j = 1;
			break;
		GETOPT_OPTARG("-k"):
			if (opt_k)
				usage();
			opt_k = optarg;
			break;
		GETOPT_OPTARG("-n"):
			if (opt_n != 0)
				usage();
			if (PARSENUM(&opt_n, optarg, 1, INT_MAX))
				OPT_EPARSE("-n", optarg);
			break;
		GETOPT_OPTARG("-o"):
			if (opt_o != 0.0)
				usage();
			if (PARSENUM(&opt_o, optarg, 0.001, 1000000.0))
				OPT_EPARSE("-o", optarg);
			break;
		GETOPT_OPTARG("-p"):
			if (opt_p)
				usage();
			opt_p = optarg;
			break;
		GETOPT_OPTARG("-t"):
			if (tgt)
				usage();
			tgt = optarg;
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "spipe @VERSION@\n");
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s", ch);
			usage();
		GETOPT_DEFAULT:
			warn0("illegal option -- %s", ch);
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have processed all the arguments. */
	if (argc != 1)
		usage();
	(void)argv; /* argv[0] is not used beyond this point. */

	/* Set defaults. */
	if (opt_n == 0)
		opt_n = 100;

	/* Sanity-check options. */
	if (!opt_f && !opt_g)
		usage();
	if (tgt == NULL)
		usage();
	if (opt_k == NULL)
		usage();

	/* Figure out where we're going to listen. */
	if ((sas_s = sock_resolve(argv[0])) == NULL) {
		warnp("Error resolving socket address: %s", argv[0]);
		exit(1);
	}
	if (sas_s[0] == NULL) {
		warn0("No addresses found for %s", argv[0]);
		exit(1);
	}

	/* Figure out where we're going to connect to. */
	if ((sas_t = sock_resolve(tgt)) == NULL) {
		warnp("Error resolving socket address: %s", tgt);
		exit(1);
	}
	if (sas_t[0] == NULL) {
		warn0("No addresses found for %s", tgt);
		exit(1);
	}

	/* Load the keying data. */
	if ((K = proto_crypt_secret(opt_k)) == NULL) {
		warnp("Error reading shared secret");
		exit(1);
	}

	/*
	 * Create a socket, bind it, mark it as listening, and mark it as
	 * non-blocking.
	 */
	if ((sa_b = sock_listener(sas_s, &s[0])) == NULL)
		goto err2;

	/* Daemonize early if we're going to wait for a connection. */
	if (opt_D && opt_g) {
		if (asprintf(&opt_p, "%s.tmp", opt_p) == -1) {
			warnp("asprintf");
			goto err3;
		}
		if (daemonize(opt_p)) {
			warnp("Failed to daemonize");
			goto err3;
		}
	}

	/* Create a socket pair for communication. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s)) {
		warnp("socketpair");
		goto err3;
	}

	/* Set up the shared state. */
	ET.s = s[1];
	ET.sas = sas_t[0];
	ET.tgt = tgt;
	ET.K = K;
	ET.nofps = opt_F ? 1 : 0;
	ET.nokeepalive = opt_j ? 1 : 0;
	ET.timeo = opt_o;
	ET.stopped = 0;

	/* Set up signal handler. */
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = signal_handler;
	if (sigaction(SIGUSR1, &sa, NULL)) {
		warnp("sigaction");
		goto err4;
	}

	/* Launch threads. */
	if ((ET.threads = malloc(2 * sizeof(pthread_t))) == NULL)
		goto err4;
	if ((rc = pthread_create(&ET.threads[0], NULL,
	    receive_thread, &ET)) != 0) {
		warn0("pthread_create: %s", strerror(rc));
		goto err5;
	}
	if ((rc = pthread_create(&ET.threads[1], NULL,
	    accept_thread, &ET)) != 0) {
		warn0("pthread_create: %s", strerror(rc));
		ET.stopped = 1;
		pthread_kill(ET.threads[0], SIGUSR1);
		pthread_join(ET.threads[0], NULL);
		goto err5;
	}

	/* Daemonize and write pid. */
	if (opt_D && !opt_g) {
		if (daemonize(opt_p)) {
			warnp("Failed to daemonize");
			ET.stopped = 1;
			pthread_kill(ET.threads[0], SIGUSR1);
			pthread_kill(ET.threads[1], SIGUSR1);
			pthread_join(ET.threads[0], NULL);
			pthread_join(ET.threads[1], NULL);
			goto err5;
		}
	}

	/* Wait for threads to finish (if necessary) */
	if (ET.stopped == 0) {
		if ((rc = pthread_join(ET.threads[0], NULL)) != 0) {
			warn0("pthread_join: %s", strerror(rc));
			if (close(s[0]))
				warnp("close");
			proto_crypt_secret_free(K);
			sock_addr_free(sa_b);
			goto err3;
		}
		if ((rc = pthread_join(ET.threads[1], NULL)) != 0) {
			warn0("pthread_join: %s", strerror(rc));
			if (close(s[0]))
				warnp("close");
			proto_crypt_secret_free(K);
			sock_addr_free(sa_b);
			goto err3;
		}
	}

	/* Clean up. */
	if (close(s[0]))
		warnp("close");
	proto_crypt_secret_free(K);
	sock_addr_free(sa_b);
	sock_addr_freelist(sas_t);
	sock_addr_freelist(sas_s);
	free(ET.threads);

	/* Success! */
	return (0);

err5:
	free(ET.threads);
err4:
	close(s[0]);
	close(s[1]);
err3:
	close(s[0]);
err2:
	proto_crypt_secret_free(K);
	sock_addr_free(sa_b);
err1:
	sock_addr_freelist(sas_t);
err0:
	sock_addr_freelist(sas_s);

	/* Failure! */
	return (1);
}
