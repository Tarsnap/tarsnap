/*
 * PR822 affected-function regression driver.
 * FLINT authored the original real-glibc sequence/prefix reproducer.
 * QUAY packaged it with a locale probe and ordinary-output controls.
 * This file is appended to the unmodified, hash-checked function bodies.
 */
#include <errno.h>
#include <limits.h>

static const unsigned char sequences[][7] = {
	{0xf4, 0x90, 0x80, 0x80, 0, 0, 0},
	{0xf8, 0x88, 0x80, 0x80, 0x80, 0, 0},
	{0xfc, 0x84, 0x80, 0x80, 0x80, 0x80, 0}
};

static int
probe(void)
{
	const char *loc;
	wchar_t wc;
	unsigned n;
	int converted;

	loc = setlocale(LC_ALL, "C.UTF-8");
	if (loc == NULL)
		loc = setlocale(LC_ALL, "C.utf8");
	if (loc == NULL)
		return (77);
	fprintf(stderr, "glibc=%s locale=%s MB_CUR_MAX=%zu MB_LEN_MAX=%d\n",
	    gnu_get_libc_version(), loc, (size_t)MB_CUR_MAX, MB_LEN_MAX);
	for (n = 4; n <= 6; n++) {
		wc = 0;
		mbtowc(NULL, NULL, 0);
		converted = mbtowc(&wc, (const char *)sequences[n - 4], n);
		fprintf(stderr, "n=%u converted=%d wc=0x%lx printable=%d\n",
		    n, converted, (unsigned long)wc, !!iswprint(wc));
		if (converted != (int)n || iswprint(wc))
			return (77);
	}
	return (0);
}

static int
number(const char *text, unsigned long maximum, unsigned *value)
{
	char *end;
	unsigned long parsed;

	if (*text < '0' || *text > '9')
		return (-1);
	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno || *end != '\0' || parsed > maximum)
		return (-1);
	*value = (unsigned)parsed;
	return (0);
}

int
main(int argc, char **argv)
{
	char input[520];
	unsigned prefix, n, control;
	int rc;
	const char *controls[] = {
		"", "ordinary ASCII", "\\", "\a\b\f\n\r\t\v",
		"\xc3\xa9", "\xe2\x82\xac", "\xff\xc3\xa9",
		"A\n\\\tZ"
	};

	rc = probe();
	if (rc != 0)
		return (rc);
	if (argc == 2 && strcmp(argv[1], "probe") == 0)
		return (0);
	if (argc == 3 && strcmp(argv[1], "control") == 0) {
		if (number(argv[2], 7, &control) != 0)
			return (64);
		safe_fprintf(stdout, "%s", controls[control]);
		return (ferror(stdout) ? 74 : 0);
	}
	if (argc != 3 || number(argv[1], 512, &prefix) != 0 ||
	    number(argv[2], 6, &n) != 0 || n < 4)
		return (64);
	memset(input, 'A', prefix);
	memcpy(input + prefix, sequences[n - 4], n);
	input[prefix + n] = '\0';
	safe_fprintf(stdout, "%s", input);
	return (ferror(stdout) ? 74 : 0);
}
