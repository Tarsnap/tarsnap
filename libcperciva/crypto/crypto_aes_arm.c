#include "cpusupport.h"
#ifdef CPUSUPPORT_ARM_AES
/**
 * CPUSUPPORT CFLAGS: ARM_AES
 */

#include <stdint.h>
#include <stdlib.h>
#include <wmmintrin.h>

#include "align_ptr.h"
#include "insecure_memzero.h"
#include "warnp.h"

#include "crypto_aes_arm.h"
#include "crypto_aes_arm_m128i.h"

/* Expanded-key structure. */
struct crypto_aes_key_arm {
	ALIGN_PTR_DECL(__m128i, rkeys, 15, sizeof(__m128i));
	size_t nr;
};

/* Compute an AES-128 round key. */
#define MKRKEY128(rkeys, i, rcon) do {				\
	__m128i _s = rkeys[i - 1];				\
	__m128i _t = rkeys[i - 1];				\
	_s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));		\
	_s = _mm_xor_si128(_s, _mm_slli_si128(_s, 8));		\
	_t = _mm_aeskeygenassist_si128(_t, rcon);		\
	_t = _mm_shuffle_epi32(_t, 0xff);			\
	rkeys[i] = _mm_xor_si128(_s, _t);			\
} while (0)

/**
 * crypto_aes_key_expand_128_arm(key, rkeys):
 * Expand the 128-bit AES key ${key} into the 11 round keys ${rkeys}.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
static void
crypto_aes_key_expand_128_arm(const uint8_t key[16], __m128i rkeys[11])
{

	/* The first round key is just the key. */
	/*-
	 * XXX Compiler breakage:
	 * The intrinsic defined by Intel for _mm_loadu_si128 defines it as
	 * taking a (const __m128i *) parameter.  This forces us to write a
	 * bug: The cast to (const __m128i *) is invalid since it increases
	 * the alignment requirement of the pointer.  Alas, until compilers
	 * get fixed intrinsics, all we can do is code the bug and require
	 * that alignment-requirement-increasing compiler warnings get
	 * disabled.
	 */
	rkeys[0] = _mm_loadu_si128((const __m128i *)&key[0]);

	/*
	 * Each of the remaining round keys are computed from the preceding
	 * round key: rotword+subword+rcon (provided as aeskeygenassist) to
	 * compute the 'temp' value, then xor with 1, 2, 3, or all 4 of the
	 * 32-bit words from the preceding round key.  Unfortunately, 'rcon'
	 * is encoded as an immediate value, so we need to write the loop out
	 * ourselves rather than allowing the compiler to expand it.
	 */
	MKRKEY128(rkeys, 1, 0x01);
	MKRKEY128(rkeys, 2, 0x02);
	MKRKEY128(rkeys, 3, 0x04);
	MKRKEY128(rkeys, 4, 0x08);
	MKRKEY128(rkeys, 5, 0x10);
	MKRKEY128(rkeys, 6, 0x20);
	MKRKEY128(rkeys, 7, 0x40);
	MKRKEY128(rkeys, 8, 0x80);
	MKRKEY128(rkeys, 9, 0x1b);
	MKRKEY128(rkeys, 10, 0x36);
}

/* Compute an AES-256 round key. */
#define MKRKEY256(rkeys, i, shuffle, rcon)	do {		\
	__m128i _s = rkeys[i - 2];				\
	__m128i _t = rkeys[i - 1];				\
	_s = _mm_xor_si128(_s, _mm_slli_si128(_s, 4));		\
	_s = _mm_xor_si128(_s, _mm_slli_si128(_s, 8));		\
	_t = _mm_aeskeygenassist_si128(_t, rcon);		\
	_t = _mm_shuffle_epi32(_t, shuffle);			\
	rkeys[i] = _mm_xor_si128(_s, _t);			\
} while (0)

/**
 * crypto_aes_key_expand_256_arm(key, rkeys):
 * Expand the 256-bit AES key ${key} into the 15 round keys ${rkeys}.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
static void
crypto_aes_key_expand_256_arm(const uint8_t key[32], __m128i rkeys[15])
{

	/* The first two round keys are just the key. */
	/*-
	 * XXX Compiler breakage:
	 * The intrinsic defined by Intel for _mm_loadu_si128 defines it as
	 * taking a (const __m128i *) parameter.  This forces us to write a
	 * bug: The cast to (const __m128i *) is invalid since it increases
	 * the alignment requirement of the pointer.  Alas, until compilers
	 * get fixed intrinsics, all we can do is code the bug and require
	 * that alignment-requirement-increasing compiler warnings get
	 * disabled.
	 */
	rkeys[0] = _mm_loadu_si128((const __m128i *)&key[0]);
	rkeys[1] = _mm_loadu_si128((const __m128i *)&key[16]);

	/*
	 * Each of the remaining round keys are computed from the preceding
	 * pair of keys.  Even rounds use rotword+subword+rcon, while odd
	 * rounds just use subword; the aeskeygenassist instruction computes
	 * both, and we use 0xff or 0xaa to select the one we need.  The rcon
	 * value used is irrelevant for odd rounds since we ignore the value
	 * which it feeds into.  Unfortunately, the 'shuffle' and 'rcon'
	 * values are encoded into the instructions as immediates, so we need
	 * to write the loop out ourselves rather than allowing the compiler
	 * to expand it.
	 */
	MKRKEY256(rkeys, 2, 0xff, 0x01);
	MKRKEY256(rkeys, 3, 0xaa, 0x00);
	MKRKEY256(rkeys, 4, 0xff, 0x02);
	MKRKEY256(rkeys, 5, 0xaa, 0x00);
	MKRKEY256(rkeys, 6, 0xff, 0x04);
	MKRKEY256(rkeys, 7, 0xaa, 0x00);
	MKRKEY256(rkeys, 8, 0xff, 0x08);
	MKRKEY256(rkeys, 9, 0xaa, 0x00);
	MKRKEY256(rkeys, 10, 0xff, 0x10);
	MKRKEY256(rkeys, 11, 0xaa, 0x00);
	MKRKEY256(rkeys, 12, 0xff, 0x20);
	MKRKEY256(rkeys, 13, 0xaa, 0x00);
	MKRKEY256(rkeys, 14, 0xff, 0x40);
}

/**
 * crypto_aes_key_expand_arm(key, len):
 * Expand the ${len}-byte AES key ${key} into a structure which can be passed
 * to crypto_aes_encrypt_block_arm().  The length must be 16 or 32.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
void *
crypto_aes_key_expand_arm(const uint8_t * key, size_t len)
{
	struct crypto_aes_key_arm * kexp;

	/* Allocate structure. */
	if ((kexp = malloc(sizeof(struct crypto_aes_key_arm))) == NULL)
		goto err0;

	/* Figure out where to put the round keys. */
	ALIGN_PTR_INIT(kexp->rkeys, sizeof(__m128i));

	/* Compute round keys. */
	if (len == 16) {
		kexp->nr = 10;
		crypto_aes_key_expand_128_arm(key, kexp->rkeys);
	} else if (len == 32) {
		kexp->nr = 14;
		crypto_aes_key_expand_256_arm(key, kexp->rkeys);
	} else {
		warn0("Unsupported AES key length: %zu bytes", len);
		goto err1;
	}

	/* Success! */
	return (kexp);

err1:
	free(kexp);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_aes_encrypt_block_arm_m128i(in, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and return the
 * resulting ciphertext.  This implementation uses ARM AES instructions,
 * and should only be used if CPUSUPPORT_ARM_AES is defined and
 * cpusupport_arm_aes() returns nonzero.
 */
__m128i
crypto_aes_encrypt_block_arm_m128i(__m128i in, const void * key)
{
	const struct crypto_aes_key_arm * _key = key;
	const __m128i * aes_key = _key->rkeys;
	__m128i aes_state = in;
	size_t nr = _key->nr;

	aes_state = _mm_xor_si128(aes_state, aes_key[0]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[1]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[2]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[3]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[4]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[5]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[6]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[7]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[8]);
	aes_state = _mm_aesenc_si128(aes_state, aes_key[9]);
	if (nr > 10) {
		aes_state = _mm_aesenc_si128(aes_state, aes_key[10]);
		aes_state = _mm_aesenc_si128(aes_state, aes_key[11]);
		aes_state = _mm_aesenc_si128(aes_state, aes_key[12]);
		aes_state = _mm_aesenc_si128(aes_state, aes_key[13]);
	}

	aes_state = _mm_aesenclast_si128(aes_state, aes_key[nr]);
	return (aes_state);
}

/**
 * crypto_aes_encrypt_block_arm(in, out, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and write the
 * resulting ciphertext to ${out}.  ${in} and ${out} can overlap.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
void
crypto_aes_encrypt_block_arm(const uint8_t in[16], uint8_t out[16],
    const void * key)
{
	__m128i aes_state;

	aes_state = _mm_loadu_si128((const __m128i *)in);
	aes_state = crypto_aes_encrypt_block_arm_m128i(aes_state, key);
	_mm_storeu_si128((__m128i *)out, aes_state);
}

/**
 * crypto_aes_key_free_arm(key):
 * Free the expanded AES key ${key}.
 */
void
crypto_aes_key_free_arm(void * key)
{

	/* Behave consistently with free(NULL). */
	if (key == NULL)
		return;

	/* Attempt to zero the expanded key. */
	insecure_memzero(key, sizeof(struct crypto_aes_key_arm));

	/* Free the key. */
	free(key);
}

#endif /* CPUSUPPORT_ARM_AES */
