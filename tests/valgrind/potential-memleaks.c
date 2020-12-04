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

#define MEMLEAKTEST(x) { #x, x }
static const struct memleaktest {
	const char * const name;
	void (* const volatile func)(void);
} tests[] = {
	MEMLEAKTEST(pl_nothing),
	MEMLEAKTEST(pl_freebsd_strerror)
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
