#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_SSE2

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <immintrin.h>

#include "sysendian.h"

#include "sha256_sse2.h"

/*
 * Decode a big-endian length len vector of (uint8_t) into a length
 * len/4 vector of (uint32_t).  Assumes len is a multiple of 4.
 */
static void
be32dec_vect(uint32_t * dst, const uint8_t * src, size_t len)
{
	size_t i;

	/* Sanity-check. */
	assert(len % 4 == 0);

	/* Decode vector, one word at a time. */
	for (i = 0; i < len / 4; i++)
		dst[i] = be32dec(src + i * 4);
}

/* SHA256 round constants. */
static const uint32_t Krnd[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Elementary functions used by SHA256 */
#define Ch(x, y, z)	((x & (y ^ z)) ^ z)
#define Maj(x, y, z)	((x & (y | z)) | (y & z))
#define ROTR(x, n)	((x >> n) | (x << (32 - n)))
#define S0(x)		(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)		(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))

/* SHA256 round function */
#define RND(a, b, c, d, e, f, g, h, k)			\
	h += S1(e) + Ch(e, f, g) + k;			\
	d += h;						\
	h += S0(a) + Maj(a, b, c)

/* Adjusted round function for rotating state */
#define RNDr(S, W, i, ii)			\
	RND(S[(64 - i) % 8], S[(65 - i) % 8],	\
	    S[(66 - i) % 8], S[(67 - i) % 8],	\
	    S[(68 - i) % 8], S[(69 - i) % 8],	\
	    S[(70 - i) % 8], S[(71 - i) % 8],	\
	    W[i + ii] + Krnd[i + ii])

/* Message schedule computation */
#define SHR32(x, n) (_mm_srli_epi32(x, n))
#define ROTR32(x, n) (_mm_or_si128(SHR32(x, n), _mm_slli_epi32(x, (32-n))))
#define s0_128(x) _mm_xor_si128(_mm_xor_si128(			\
	ROTR32(x, 7), ROTR32(x, 18)), SHR32(x, 3))
#define s1_128(x) _mm_xor_si128(_mm_xor_si128(			\
	ROTR32(x, 17), ROTR32(x, 19)), SHR32(x, 10))

/**
 * SPAN_ONE_THREE(a, b):
 * Combine the lowest word of ${a} with the upper three words of ${b}.  This
 * could also be thought of returning bits [159:32] of the 256-bit value
 * consisting of (a[127:0] b[127:0]).  In other words, set:
 *     dst[31:0] := b[63:32]
 *     dst[63:32] := b[95:64]
 *     dst[95:64] := b[127:96]
 *     dst[127:96] := a[31:0]
 */
#define SPAN_ONE_THREE(a, b) (_mm_shuffle_epi32(_mm_castps_si128(	\
	_mm_move_ss(_mm_castsi128_ps(a), _mm_castsi128_ps(b))),		\
	_MM_SHUFFLE(0, 3, 2, 1)))

static inline void
MSG4(uint32_t W[64], int ii, int i)
{
	__m128i X0, X1, X2, X3;
	__m128i X4;
	__m128i Xj_minus_seven, Xj_minus_fifteen;
	int j;

	/*
	 * Most algorithms express "the next unknown value of the message
	 * schedule" as ${W[i]}, and write other indices relative to ${i}.
	 * Unfortunately, our main loop uses ${i} to indicate 0, 16, 32, or
	 * 48.  To avoid confusion, this implementation of the message
	 * scheduling will use ${W[j]} to indicate "the next unknown value".
	 */
	j = i + ii + 16;

	/* Set up variables with various portions of W. */
	X0 = _mm_loadu_si128((const __m128i *)&W[j - 16]);
	X1 = _mm_loadu_si128((const __m128i *)&W[j - 12]);
	X2 = _mm_loadu_si128((const __m128i *)&W[j - 8]);
	X3 = _mm_loadu_si128((const __m128i *)&W[j - 4]);
	Xj_minus_seven = SPAN_ONE_THREE(X2, X3);
	Xj_minus_fifteen = SPAN_ONE_THREE(X0, X1);

	/* Begin computing X4. */
	X4 = _mm_add_epi32(X0, Xj_minus_seven);
	X4 = _mm_add_epi32(X4, s0_128(Xj_minus_fifteen));

	/* First half of s1. */
	X4 = _mm_add_epi32(X4, s1_128(_mm_srli_si128(X3, 8)));

	/* Second half of s1; this depends on the above value of X4. */
	X4 = _mm_add_epi32(X4, s1_128(_mm_slli_si128(X4, 8)));

	/* Update W. */
	_mm_storeu_si128((__m128i *)&W[j], X4);
}

/**
 * SHA256_Transform_sse2(state, block):
 * Compute the SHA256 block compression function, transforming ${state} using
 * the data in ${block}.  This implementation uses x86 SSE2 instructions, and
 * should only be used if _SSE2 is defined and cpusupport_x86_sse2() returns
 * nonzero.  The arrays W and S may be filled with sensitive data, and should
 * be cleared by the callee.
 */
#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
void
SHA256_Transform_sse2(uint32_t state[8], const uint8_t block[64],
    uint32_t W[64], uint32_t S[8])
#else
void
SHA256_Transform_sse2(uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8])
#endif
{
	int i;

	/* 1. Prepare the first part of the message schedule W. */
	be32dec_vect(W, block, 64);

	/* 2. Initialize working variables. */
	memcpy(S, state, 32);

	/* 3. Mix. */
	for (i = 0; i < 64; i += 16) {
		RNDr(S, W, 0, i);
		RNDr(S, W, 1, i);
		RNDr(S, W, 2, i);
		RNDr(S, W, 3, i);
		RNDr(S, W, 4, i);
		RNDr(S, W, 5, i);
		RNDr(S, W, 6, i);
		RNDr(S, W, 7, i);
		RNDr(S, W, 8, i);
		RNDr(S, W, 9, i);
		RNDr(S, W, 10, i);
		RNDr(S, W, 11, i);
		RNDr(S, W, 12, i);
		RNDr(S, W, 13, i);
		RNDr(S, W, 14, i);
		RNDr(S, W, 15, i);

		if (i == 48)
			break;
		MSG4(W, 0, i);
		MSG4(W, 4, i);
		MSG4(W, 8, i);
		MSG4(W, 12, i);
	}

	/* 4. Mix local working variables into global state. */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}
#endif /* CPUSUPPORT_X86_SSE2 */
