/* PR820 test plumbing; the complete production switch arm is inserted below.
 * The parser itself is not reimplemented. C/Claude owns the original fix.
 */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture_bsdtar {
	const char *optarg;
	int strip_components;
};

static void
bsdtar_errc(struct fixture_bsdtar *bsdtar, int status, int code,
    const char *format, ...)
{
	va_list ap;

	(void)bsdtar;
	(void)code;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(status);
}

int
main(int argc, char **argv)
{
	struct fixture_bsdtar state, *bsdtar = &state;
	enum { OPTION_STRIP_COMPONENTS = 1 };

	if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
		printf("INT_MAX=%d LONG_MAX=%ld int_bytes=%zu long_bytes=%zu\n",
		    INT_MAX, LONG_MAX, sizeof(int), sizeof(long));
		return (sizeof(int) == 4 && sizeof(long) == 8 ? 0 : 77);
	}
	if (argc != 2)
		return (64);
	state.optarg = argv[1];
	state.strip_components = -123;
	errno = ERANGE; /* The actual arm must clear an inherited errno. */
	switch (OPTION_STRIP_COMPONENTS) {
/* PR820_CASE */
	}
	printf("%d\n", bsdtar->strip_components);
	return (0);
}
