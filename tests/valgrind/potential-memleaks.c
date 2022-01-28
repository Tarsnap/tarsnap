#include <sys/socket.h>

#include <netinet/in.h>

#include <locale.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__FreeBSD__)
/*
 * We only want to enable this check on FreeBSD, because that OS can use
 * pthread_atfork() without explicitly linking to pthread.
 */
#include <pthread.h>
#define CHECK_PTHREAD_ATFORK
#endif

/* Checking merely linking. */
static void
pl_nothing(void)
{
	/* Do nothing. */
}

/* Problem with FreeBSD 12.0 and strerror(). */
static void
pl_freebsd_strerror(void)
{
	char * str = strerror(0);

	(void)str; /* UNUSED */
}

/* Problem with FreeBSD 10.3 fgets() with stdin. */
#define FGETS_BUFSIZE 64
static void
pl_freebsd_fgets(void)
{
	char buf[FGETS_BUFSIZE];

	if (fgets(buf, FGETS_BUFSIZE, stdin) == NULL)
		exit(1);
}

/* Problem with FreeBSD 12.0 and getpwuid(). */
static void
pl_freebsd_getpwuid(void)
{
	struct passwd * pwd;

	if ((pwd = getpwuid(0)) == NULL) {
		fprintf(stderr, "getpwuid\n");
		exit(1);
	}

	(void)pwd; /* not used beyond this point. */

	/* POSIX says that we *shall not* free `pwd`. */
}

/* Problem with FreeBSD 12.1 and setlocale(). */
static void
pl_freebsd_setlocale(void)
{

	if (setlocale(LC_ALL, "") == NULL) {
		fprintf(stderr, "setlocale failure\n");
		exit(1);
	}
}

/* Problem with FreeBSD and getaddrinfo. */
static void
pl_freebsd_getaddrinfo(const char * addr)
{
	struct addrinfo hints;
	struct addrinfo * res;
	const char * ports = "9279";
	int error;

	/* Create hints structure. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	/* Perform DNS lookup. */
	if ((error = getaddrinfo(addr, ports, &hints, &res)) != 0)
		fprintf(stderr, "Error looking up %s: %s", addr,
		    gai_strerror(error));

	/* Clean up. */
	freeaddrinfo(res);
}

/* Don't prepend pl_ here, so that we cut off pl_internal_getaddrinfo. */
static void
freebsd_getaddrinfo_localhost(void)
{

	pl_freebsd_getaddrinfo("localhost");
}

static void
freebsd_getaddrinfo_online(void)
{

	pl_freebsd_getaddrinfo("google.com");
}

#if defined(CHECK_PTHREAD_ATFORK)
/* This leak was exposed by LibreSSL's arc4random4.c & arc4random_freebsd.h. */
static void
pl_freebsd_pthread_atfork(void)
{

	pthread_atfork(NULL, NULL, NULL);
}
#endif

static void
pl_freebsd_setvbuf(void)
{

	setvbuf(stdout, NULL, _IOLBF, 0);
}

#define MEMLEAKTEST(x) { #x, x }
static const struct memleaktest {
	const char * const name;
	void (* const volatile func)(void);
} tests[] = {
	MEMLEAKTEST(pl_nothing),
	MEMLEAKTEST(pl_freebsd_strerror),
	MEMLEAKTEST(pl_freebsd_fgets),
	MEMLEAKTEST(pl_freebsd_getpwuid),
	MEMLEAKTEST(pl_freebsd_setlocale),
	MEMLEAKTEST(freebsd_getaddrinfo_localhost),
	MEMLEAKTEST(freebsd_getaddrinfo_online),
#if defined(CHECK_PTHREAD_ATFORK)
	MEMLEAKTEST(pl_freebsd_pthread_atfork),
#endif
	MEMLEAKTEST(pl_freebsd_setvbuf)
};
static const int num_tests = sizeof(tests) / sizeof(tests[0]);

int
main(int argc, char * argv[])
{
	int i;

	if (argc == 2) {
		/* Run the relevant function. */
		for (i = 0; i < num_tests; i++) {
			if ((strcmp(argv[1], tests[i].name)) == 0)
				tests[i].func();
		}
	} else {
		/* Print test names. */
		for (i = 0; i < num_tests; i++)
			printf("%s\n", tests[i].name);
	}

	/* Success! */
	exit(0);
}
