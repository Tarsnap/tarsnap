#include <emmintrin.h>

static char a[16];

/*
 * Use a separate function for this, because that means that the alignment of
 * the _mm_loadu_si128() will move to function level, which may require
 * -Wno-cast-align.
 */
static __m128i
load_128(const char * src)
{
	__m128i x;

	x = _mm_loadu_si128((const __m128i *)src);
	return (x);
}

int
main(void)
{
	__m128i x;

	x = load_128(a);
	_mm_storeu_si128((__m128i *)a, x);
	return (a[0]);
}
