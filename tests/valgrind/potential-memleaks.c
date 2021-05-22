#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define MEMLEAKTEST(x) { #x, x }
static const struct memleaktest {
	const char * const name;
	void (* const volatile func)(void);
} tests[] = {
	MEMLEAKTEST(pl_nothing),
	MEMLEAKTEST(pl_freebsd_strerror),
	MEMLEAKTEST(pl_freebsd_fgets),
	MEMLEAKTEST(pl_freebsd_getpwuid),
	MEMLEAKTEST(pl_freebsd_setlocale)
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
