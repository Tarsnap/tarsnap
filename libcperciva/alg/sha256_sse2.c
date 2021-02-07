#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_SSE2

#include <assert.h>
#include <stdint.h>
#include <string.h>

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
#define SHR(x, n)	(x >> n)
#define ROTR(x, n)	((x >> n) | (x << (32 - n)))
#define S0(x)		(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)		(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x)		(ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define s1(x)		(ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

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
static inline void
MSG4(uint32_t W[64], int ii, int i)
{
	int j;

	/*
	 * Most algorithms express "the next unknown value of the message
	 * schedule" as ${W[i]}, and write other indices relative to ${i}.
	 * Unfortunately, our main loop uses ${i} to indicate 0, 16, 32, or
	 * 48.  To avoid confusion, this implementation of the message
	 * scheduling will use ${W[j]} to indicate "the next unknown value".
	 */
	j = i + ii + 16;

	W[j + 0] = s1(W[j - 2 + 0]) + W[j - 7 + 0] + s0(W[j - 15 + 0]) + W[j - 16 + 0];
	W[j + 1] = s1(W[j - 2 + 1]) + W[j - 7 + 1] + s0(W[j - 15 + 1]) + W[j - 16 + 1];
	W[j + 2] = s1(W[j - 2 + 2]) + W[j - 7 + 2] + s0(W[j - 15 + 2]) + W[j - 16 + 2];
	W[j + 3] = s1(W[j - 2 + 3]) + W[j - 7 + 3] + s0(W[j - 15 + 3]) + W[j - 16 + 3];
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
