#include <immintrin.h>
#include <stdint.h>

/*
 * Use a separate function for this, because that means that the alignment of
 * the _mm_loadu_si128() will move to function level, which may require
 * -Wno-cast-align.
 */
static __m128i
load_128(const uint8_t * src)
{
	__m128i x;

	x = _mm_loadu_si128((const __m128i *)src);
	return (x);
}

int
main(void)
{
	__m128i x;
	uint8_t a[16];

	x = load_128(a);
	x = _mm_sha256msg1_epu32(x, x);
	_mm_storeu_si128((__m128i *)a, x);
	return (a[0]);
}
