#include <stdint.h>

#include <wmmintrin.h>

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
	__m128i x, y;
	uint8_t a[16];

	x = load_128(a);
#ifdef BROKEN_MM_LOADU_SI64
	y = _mm_loadu_si128(a);
#else
	y = _mm_loadu_si64(a);
#endif
	y = _mm_aesenc_si128(x, y);
	_mm_storeu_si128((__m128i *)&a[0], y);
	return (a[0]);
}
