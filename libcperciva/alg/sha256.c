#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "cpusupport.h"
#include "insecure_memzero.h"
#include "sha256_arm.h"
#include "sha256_shani.h"
#include "sha256_sse2.h"
#include "sysendian.h"
#include "warnp.h"

#include "sha256.h"

#if defined(CPUSUPPORT_X86_SHANI) && defined(CPUSUPPORT_X86_SSSE3) ||	\
    defined(CPUSUPPORT_X86_SSE2) ||					\
    defined(CPUSUPPORT_ARM_SHA256)
#define HWACCEL

static enum {
	HW_SOFTWARE = 0,
#if defined(CPUSUPPORT_X86_SHANI) && defined(CPUSUPPORT_X86_SSSE3)
	HW_X86_SHANI,
#endif
#if defined(CPUSUPPORT_X86_SSE2)
	HW_X86_SSE2,
#endif
#if defined(CPUSUPPORT_ARM_SHA256)
	HW_ARM_SHA256,
#endif
	HW_UNSET
} hwaccel = HW_UNSET;
#endif

#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
static void SHA256_Transform(uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8]);
#else
static void SHA256_Transform(uint32_t[static restrict 8],
    const uint8_t[static restrict 64], uint32_t[static restrict 64],
    uint32_t[static restrict 8]);
#endif

/*
 * Encode a length len/4 vector of (uint32_t) into a length len vector of
 * (uint8_t) in big-endian form.  Assumes len is a multiple of 4.
 */
static void
be32enc_vect(uint8_t * dst, const uint32_t * src, size_t len)
{
	size_t i;

	/* Sanity-check. */
	assert(len % 4 == 0);

	/* Encode vector, one word at a time. */
	for (i = 0; i < len / 4; i++)
		be32enc(dst + i * 4, src[i]);
}

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

/* Magic initialization constants. */
static const uint32_t initial_state[8] = {
	0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

#ifdef HWACCEL
#if defined(CPUSUPPORT_X86_SHANI) && defined(CPUSUPPORT_X86_SSSE3)
/* Shim so that we can test SHA256_Transform_shani() in the standard manner. */
static void
SHA256_Transform_shani_with_W_S(uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8])
{

	(void)W; /* UNUSED */
	(void)S; /* UNUSED */

	SHA256_Transform_shani(state, block);
}
#endif
#if defined(CPUSUPPORT_ARM_SHA256)
/* Shim so that we can test SHA256_Transform_arm() in the standard manner. */
static void
SHA256_Transform_arm_with_W_S(uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8])
{

	(void)W; /* UNUSED */
	(void)S; /* UNUSED */

	SHA256_Transform_arm(state, block);
}
#endif

/*
 * Test whether software and hardware extensions transform code produce the
 * same results.  Must be called with (hwaccel == HW_SOFTWARE).
 */
static int
hwtest(const uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64],
    uint32_t W[static restrict 64], uint32_t S[static restrict 8],
    void(* func)(uint32_t [static restrict 8],
    const uint8_t [static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8]))
{
	uint32_t state_sw[8];
	uint32_t state_hw[8];

	/* Software transform. */
	memcpy(state_sw, state, sizeof(state_sw));
	SHA256_Transform(state_sw, block, W, S);

	/* Hardware transform. */
	memcpy(state_hw, state, sizeof(state_hw));
	func(state_hw, block, W, S);

	/* Do the results match? */
	return (memcmp(state_sw, state_hw, sizeof(state_sw)));
}

/* Which type of hardware acceleration should we use, if any? */
static void
hwaccel_init(void)
{
	uint32_t W[64];
	uint32_t S[8];
	uint8_t block[64];
	uint8_t i;

	/* If we've already set hwaccel, we're finished. */
	if (hwaccel != HW_UNSET)
		return;

	/* Default to software. */
	hwaccel = HW_SOFTWARE;

	/* Test case: Hash 0x00 0x01 0x02 ... 0x3f. */
	for (i = 0; i < 64; i++)
		block[i] = i;

#if defined(CPUSUPPORT_X86_SHANI) && defined(CPUSUPPORT_X86_SSSE3)
	CPUSUPPORT_VALIDATE(hwaccel, HW_X86_SHANI,
	    cpusupport_x86_shani() && cpusupport_x86_ssse3(),
	    hwtest(initial_state, block, W, S,
		SHA256_Transform_shani_with_W_S));
#endif
#if defined(CPUSUPPORT_X86_SSE2)
	CPUSUPPORT_VALIDATE(hwaccel, HW_X86_SSE2, cpusupport_x86_sse2(),
	    hwtest(initial_state, block, W, S, SHA256_Transform_sse2));
#endif
#if defined(CPUSUPPORT_ARM_SHA256)
	CPUSUPPORT_VALIDATE(hwaccel, HW_ARM_SHA256, cpusupport_arm_sha256(),
	    hwtest(initial_state, block, W, S, SHA256_Transform_arm_with_W_S));
#endif
}
#endif /* HWACCEL */

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
#define MSCH(W, ii, i)				\
	W[i + ii + 16] = s1(W[i + ii + 14]) + W[i + ii + 9] + s0(W[i + ii + 1]) + W[i + ii]

/*
 * SHA256 block compression function.  The 256-bit state is transformed via
 * the 512-bit input block to produce a new state.  The arrays W and S may be
 * filled with sensitive data, and should be sanitized by the callee.
 */
static void
SHA256_Transform(uint32_t state[static restrict 8],
    const uint8_t block[static restrict 64],
    uint32_t W[static restrict 64], uint32_t S[static restrict 8])
{
	int i;

#ifdef HWACCEL

#if defined(__GNUC__) && defined(__aarch64__)
	/*
	 * We require that SHA256_Init() is called before SHA256_Transform(),
	 * but the compiler has no way of knowing that.  This assert adds a
	 * significant speed boost for gcc on 64-bit ARM, and a minor penalty
	 * on other systems & compilers.
	 */
	assert(hwaccel != HW_UNSET);
#endif

	switch(hwaccel) {
#if defined(CPUSUPPORT_X86_SHANI) && defined(CPUSUPPORT_X86_SSSE3)
	case HW_X86_SHANI:
		SHA256_Transform_shani(state, block);
		return;
#endif
#if defined(CPUSUPPORT_X86_SSE2)
	case HW_X86_SSE2:
		SHA256_Transform_sse2(state, block, W, S);
		return;
#endif
#if defined(CPUSUPPORT_ARM_SHA256)
	case HW_ARM_SHA256:
		SHA256_Transform_arm(state, block);
		return;
#endif
	case HW_SOFTWARE:
	case HW_UNSET:
		break;
	}
#endif /* HWACCEL */

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
		MSCH(W, 0, i);
		MSCH(W, 1, i);
		MSCH(W, 2, i);
		MSCH(W, 3, i);
		MSCH(W, 4, i);
		MSCH(W, 5, i);
		MSCH(W, 6, i);
		MSCH(W, 7, i);
		MSCH(W, 8, i);
		MSCH(W, 9, i);
		MSCH(W, 10, i);
		MSCH(W, 11, i);
		MSCH(W, 12, i);
		MSCH(W, 13, i);
		MSCH(W, 14, i);
		MSCH(W, 15, i);
	}

	/* 4. Mix local working variables into global state. */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}

static const uint8_t PAD[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Add padding and terminating bit-count. */
static void
SHA256_Pad(SHA256_CTX * ctx, uint32_t tmp32[static restrict 72])
{
	size_t r;

	/* Figure out how many bytes we have buffered. */
	r = (ctx->count >> 3) & 0x3f;

	/* Pad to 56 mod 64, transforming if we finish a block en route. */
	if (r < 56) {
		/* Pad to 56 mod 64. */
		memcpy(&ctx->buf[r], PAD, 56 - r);
	} else {
		/* Finish the current block and mix. */
		memcpy(&ctx->buf[r], PAD, 64 - r);
		SHA256_Transform(ctx->state, ctx->buf, &tmp32[0], &tmp32[64]);

		/* The start of the final block is all zeroes. */
		memset(&ctx->buf[0], 0, 56);
	}

	/* Add the terminating bit-count. */
	be64enc(&ctx->buf[56], ctx->count);

	/* Mix in the final block. */
	SHA256_Transform(ctx->state, ctx->buf, &tmp32[0], &tmp32[64]);
}

/**
 * SHA256_Init(ctx):
 * Initialize the SHA256 context ${ctx}.
 */
void
SHA256_Init(SHA256_CTX * ctx)
{

	/* Zero bits processed so far. */
	ctx->count = 0;

	/* Initialize state. */
	memcpy(ctx->state, initial_state, sizeof(initial_state));

#ifdef HWACCEL
	/* Ensure that we've chosen the type of hardware acceleration. */
	hwaccel_init();
#endif
}

/**
 * SHA256_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the SHA256 context ${ctx}.
 */
static void
_SHA256_Update(SHA256_CTX * ctx, const void * in, size_t len,
    uint32_t tmp32[static restrict 72])
{
	uint32_t r;
	const uint8_t * src = in;

	/* Return immediately if we have nothing to do. */
	if (len == 0)
		return;

	/* Number of bytes left in the buffer from previous updates. */
	r = (ctx->count >> 3) & 0x3f;

	/* Update number of bits. */
	ctx->count += (uint64_t)(len) << 3;

	/* Handle the case where we don't need to perform any transforms. */
	if (len < 64 - r) {
		memcpy(&ctx->buf[r], src, len);
		return;
	}

	/* Finish the current block. */
	memcpy(&ctx->buf[r], src, 64 - r);
	SHA256_Transform(ctx->state, ctx->buf, &tmp32[0], &tmp32[64]);
	src += 64 - r;
	len -= 64 - r;

	/* Perform complete blocks. */
	while (len >= 64) {
		SHA256_Transform(ctx->state, src, &tmp32[0], &tmp32[64]);
		src += 64;
		len -= 64;
	}

	/* Copy left over data into buffer. */
	memcpy(ctx->buf, src, len);
}

/* Wrapper function for intermediate-values sanitization. */
void
SHA256_Update(SHA256_CTX * ctx, const void * in, size_t len)
{
	uint32_t tmp32[72];

	/* Call the real function. */
	_SHA256_Update(ctx, in, len, tmp32);

	/* Clean the stack. */
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
}

/**
 * SHA256_Final(digest, ctx):
 * Output the SHA256 hash of the data input to the context ${ctx} into the
 * buffer ${digest}, and clear the context state.
 */
static void
_SHA256_Final(uint8_t digest[32], SHA256_CTX * ctx,
    uint32_t tmp32[static restrict 72])
{

	/* Add padding. */
	SHA256_Pad(ctx, tmp32);

	/* Write the hash. */
	be32enc_vect(digest, ctx->state, 32);
}

/* Wrapper function for intermediate-values sanitization. */
void
SHA256_Final(uint8_t digest[32], SHA256_CTX * ctx)
{
	uint32_t tmp32[72];

	/* Call the real function. */
	_SHA256_Final(digest, ctx, tmp32);

	/* Clear the context state. */
	insecure_memzero(ctx, sizeof(SHA256_CTX));

	/* Clean the stack. */
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
}

/**
 * SHA256_Buf(in, len, digest):
 * Compute the SHA256 hash of ${len} bytes from ${in} and write it to ${digest}.
 */
void
SHA256_Buf(const void * in, size_t len, uint8_t digest[32])
{
	SHA256_CTX ctx;
	uint32_t tmp32[72];

	SHA256_Init(&ctx);
	_SHA256_Update(&ctx, in, len, tmp32);
	_SHA256_Final(digest, &ctx, tmp32);

	/* Clean the stack. */
	insecure_memzero(&ctx, sizeof(SHA256_CTX));
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
}

/**
 * HMAC_SHA256_Init(ctx, K, Klen):
 * Initialize the HMAC-SHA256 context ${ctx} with ${Klen} bytes of key from
 * ${K}.
 */
static void
_HMAC_SHA256_Init(HMAC_SHA256_CTX * ctx, const void * _K, size_t Klen,
    uint32_t tmp32[static restrict 72], uint8_t pad[static restrict 64],
    uint8_t khash[static restrict 32])
{
	const uint8_t * K = _K;
	size_t i;

	/* If Klen > 64, the key is really SHA256(K). */
	if (Klen > 64) {
		SHA256_Init(&ctx->ictx);
		_SHA256_Update(&ctx->ictx, K, Klen, tmp32);
		_SHA256_Final(khash, &ctx->ictx, tmp32);
		K = khash;
		Klen = 32;
	}

	/* Inner SHA256 operation is SHA256(K xor [block of 0x36] || data). */
	SHA256_Init(&ctx->ictx);
	memset(pad, 0x36, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	_SHA256_Update(&ctx->ictx, pad, 64, tmp32);

	/* Outer SHA256 operation is SHA256(K xor [block of 0x5c] || hash). */
	SHA256_Init(&ctx->octx);
	memset(pad, 0x5c, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	_SHA256_Update(&ctx->octx, pad, 64, tmp32);
}

/* Wrapper function for intermediate-values sanitization. */
void
HMAC_SHA256_Init(HMAC_SHA256_CTX * ctx, const void * _K, size_t Klen)
{
	uint32_t tmp32[72];
	uint8_t pad[64];
	uint8_t khash[32];

	/* Call the real function. */
	_HMAC_SHA256_Init(ctx, _K, Klen, tmp32, pad, khash);

	/* Clean the stack. */
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
	insecure_memzero(khash, 32);
	insecure_memzero(pad, 64);
}

/**
 * HMAC_SHA256_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the HMAC-SHA256 context ${ctx}.
 */
static void
_HMAC_SHA256_Update(HMAC_SHA256_CTX * ctx, const void * in, size_t len,
    uint32_t tmp32[static restrict 72])
{

	/* Feed data to the inner SHA256 operation. */
	_SHA256_Update(&ctx->ictx, in, len, tmp32);
}

/* Wrapper function for intermediate-values sanitization. */
void
HMAC_SHA256_Update(HMAC_SHA256_CTX * ctx, const void * in, size_t len)
{
	uint32_t tmp32[72];

	/* Call the real function. */
	_HMAC_SHA256_Update(ctx, in, len, tmp32);

	/* Clean the stack. */
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
}

/**
 * HMAC_SHA256_Final(digest, ctx):
 * Output the HMAC-SHA256 of the data input to the context ${ctx} into the
 * buffer ${digest}, and clear the context state.
 */
static void
_HMAC_SHA256_Final(uint8_t digest[32], HMAC_SHA256_CTX * ctx,
    uint32_t tmp32[static restrict 72], uint8_t ihash[static restrict 32])
{

	/* Finish the inner SHA256 operation. */
	_SHA256_Final(ihash, &ctx->ictx, tmp32);

	/* Feed the inner hash to the outer SHA256 operation. */
	_SHA256_Update(&ctx->octx, ihash, 32, tmp32);

	/* Finish the outer SHA256 operation. */
	_SHA256_Final(digest, &ctx->octx, tmp32);
}

/* Wrapper function for intermediate-values sanitization. */
void
HMAC_SHA256_Final(uint8_t digest[32], HMAC_SHA256_CTX * ctx)
{
	uint32_t tmp32[72];
	uint8_t ihash[32];

	/* Call the real function. */
	_HMAC_SHA256_Final(digest, ctx, tmp32, ihash);

	/* Clear the context state. */
	insecure_memzero(ctx, sizeof(HMAC_SHA256_CTX));

	/* Clean the stack. */
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
	insecure_memzero(ihash, 32);
}

/**
 * HMAC_SHA256_Buf(K, Klen, in, len, digest):
 * Compute the HMAC-SHA256 of ${len} bytes from ${in} using the key ${K} of
 * length ${Klen}, and write the result to ${digest}.
 */
void
HMAC_SHA256_Buf(const void * K, size_t Klen, const void * in, size_t len,
    uint8_t digest[32])
{
	HMAC_SHA256_CTX ctx;
	uint32_t tmp32[72];
	uint8_t tmp8[96];

	_HMAC_SHA256_Init(&ctx, K, Klen, tmp32, &tmp8[0], &tmp8[64]);
	_HMAC_SHA256_Update(&ctx, in, len, tmp32);
	_HMAC_SHA256_Final(digest, &ctx, tmp32, &tmp8[0]);

	/* Clean the stack. */
	insecure_memzero(&ctx, sizeof(HMAC_SHA256_CTX));
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
	insecure_memzero(tmp8, 96);
}

/**
 * PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, c, buf, dkLen):
 * Compute PBKDF2(passwd, salt, c, dkLen) using HMAC-SHA256 as the PRF, and
 * write the output to buf.  The value dkLen must be at most 32 * (2^32 - 1).
 */
void
PBKDF2_SHA256(const uint8_t * passwd, size_t passwdlen, const uint8_t * salt,
    size_t saltlen, uint64_t c, uint8_t * buf, size_t dkLen)
{
	HMAC_SHA256_CTX Phctx, PShctx, hctx;
	uint32_t tmp32[72];
	uint8_t tmp8[96];
	size_t i;
	uint8_t ivec[4];
	uint8_t U[32];
	uint8_t T[32];
	uint64_t j;
	int k;
	size_t clen;

#if SIZE_MAX >= (32 * UINT32_MAX)
	/* Sanity-check. */
	assert(dkLen <= 32 * (size_t)(UINT32_MAX));
#endif

	/* Compute HMAC state after processing P. */
	_HMAC_SHA256_Init(&Phctx, passwd, passwdlen,
	    tmp32, &tmp8[0], &tmp8[64]);

	/* Compute HMAC state after processing P and S. */
	memcpy(&PShctx, &Phctx, sizeof(HMAC_SHA256_CTX));
	_HMAC_SHA256_Update(&PShctx, salt, saltlen, tmp32);

	/* Iterate through the blocks. */
	for (i = 0; i * 32 < dkLen; i++) {
		/* Generate INT(i + 1). */
		be32enc(ivec, (uint32_t)(i + 1));

		/* Compute U_1 = PRF(P, S || INT(i)). */
		memcpy(&hctx, &PShctx, sizeof(HMAC_SHA256_CTX));
		_HMAC_SHA256_Update(&hctx, ivec, 4, tmp32);
		_HMAC_SHA256_Final(U, &hctx, tmp32, tmp8);

		/* T_i = U_1 ... */
		memcpy(T, U, 32);

		for (j = 2; j <= c; j++) {
			/* Compute U_j. */
			memcpy(&hctx, &Phctx, sizeof(HMAC_SHA256_CTX));
			_HMAC_SHA256_Update(&hctx, U, 32, tmp32);
			_HMAC_SHA256_Final(U, &hctx, tmp32, tmp8);

			/* ... xor U_j ... */
			for (k = 0; k < 32; k++)
				T[k] ^= U[k];
		}

		/* Copy as many bytes as necessary into buf. */
		clen = dkLen - i * 32;
		if (clen > 32)
			clen = 32;
		memcpy(&buf[i * 32], T, clen);
	}

	/* Clean the stack. */
	insecure_memzero(&Phctx, sizeof(HMAC_SHA256_CTX));
	insecure_memzero(&PShctx, sizeof(HMAC_SHA256_CTX));
	insecure_memzero(&hctx, sizeof(HMAC_SHA256_CTX));
	insecure_memzero(tmp32, sizeof(uint32_t) * 72);
	insecure_memzero(tmp8, 96);
	insecure_memzero(U, 32);
	insecure_memzero(T, 32);
}
