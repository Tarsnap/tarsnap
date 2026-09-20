/*
 * Regression for padme() using its real production implementation.
 *
 * The historical expression shifted a 32-bit int even though the shift count
 * is derived from a size_t length.  This driver compares the production helper
 * with an independent modulus-based reference around powers of two, including
 * the 2^37 boundary, without allocating a giant chunk buffer.
 */
#include "platform.h"

#include <sys/types.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../tar/chunks/chunks_write.c"

static size_t
reference_pad(size_t len, size_t maxlen)
{
	size_t e, s, q, rem, room, x;

	if (len <= 8)
		return (0);

	e = 0;
	for (x = len; x > 1; x >>= 1)
		e++;
	s = 0;
	for (x = e; x != 0; x >>= 1)
		s++;

	q = (size_t)1 << (e - s);
	rem = len % q;
	if (rem == 0)
		return (0);

	room = maxlen - len;
	if (q - rem > room)
		return (room);
	return (q - rem);
}

static int
check(size_t len, size_t maxlen)
{
	size_t got, want;

	got = padme(len, maxlen);
	want = reference_pad(len, maxlen);
	if (got != want) {
		fprintf(stderr, "FAIL: len=%zu max=%zu got=%zu want=%zu\n",
		    len, maxlen, got, want);
		return (-1);
	}
	return (0);
}

int
main(void)
{
	size_t bits, e, s, q, base, maxlen, i;
	static const size_t small[] = {
		1, 2, 7, 8, 9, 15, 16, 17, 255, 256, 257, 4095, 4096, 4097
	};

	bits = sizeof(size_t) * CHAR_BIT;
	maxlen = SIZE_MAX / 2;

	for (i = 0; i < sizeof(small) / sizeof(small[0]); i++)
		if (check(small[i], small[i]))
			return (1);

	/* The reported bug requires a size_t wider than 32 bits. */
	if (bits <= 32) {
		printf("SKIP: size_t is %zu bits\n", bits);
		return (77);
	}

	for (e = 30; e + 2 < bits; e++) {
		base = (size_t)1 << e;
		s = 0;
		for (i = e; i != 0; i >>= 1)
			s++;
		q = (size_t)1 << (e - s);

		if (check(base - 1, maxlen) ||
		    check(base, maxlen) ||
		    check(base + 1, maxlen) ||
		    check(base + q - 1, maxlen) ||
		    check(base + q, maxlen))
			return (1);
	}

	/* Explicitly pin the first undefined-int-shift threshold from #835. */
	base = (size_t)1 << 37;
	if (check(base + 1, maxlen))
		return (1);
	if (padme(base + 1, maxlen) != (((size_t)1 << 31) - 1)) {
		fprintf(stderr, "FAIL: 2^37+1 boundary padding changed\n");
		return (1);
	}

	printf("PASS: padme size_t shift boundaries through %zu-bit size_t\n", bits);
	return (0);
}
