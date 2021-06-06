#include "cpusupport.h"
#ifdef CPUSUPPORT_ARM_AES
/**
 * CPUSUPPORT CFLAGS: ARM_AES
 */

#include <stdint.h>
#include <stdlib.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "align_ptr.h"
#include "insecure_memzero.h"
#include "warnp.h"

#include "crypto_aes_arm.h"
#include "crypto_aes_arm_u8.h"

/* Expanded-key structure. */
struct crypto_aes_key_arm {
	ALIGN_PTR_DECL(uint8x16_t, rkeys, 15, sizeof(uint8x16_t));
	size_t nr;
};

/**
 * vdupq_laneq_u32_u8(a, lane):
 * Set all 32-bit vector lanes to the same value.  Exactly the same as
 * vdupq_laneq_u32(), except that accepts (and returns) uint8x16_t.
 */
#define vdupq_laneq_u32_u8(a, lane)			\
	vreinterpretq_u8_u32(vdupq_laneq_u32(vreinterpretq_u32_u8(a), lane))

/**
 * vshlq_n_u128(a, n):
 * Shift left (immediate), applied to the whole vector at once.
 *
 * Implementation note: this concatenates ${a} with a vector containing zeros,
 * then extracts a new vector from the pair (similar to a sliding window).
 * For example, vshlq_n_u128(a, 3) would do:
 *             0xaaaaaaaaaaaaaaaa0000000000000000
 *     return:      ~~~~~~~~~~~~~~~~
 * This is the recommended method of shifting an entire vector with Neon
 * intrinsics; all of the built-in shift instructions operate on multiple
 * values (such as a pair of 64-bit values).
 */
#define vshlq_n_u128(a, n) vextq_u8(vdupq_n_u8(0), a, 16 - n)

/**
 * SubWord_duplicate(a):
 * Perform the AES SubWord operation on the final 32-bit word (bits 96..127)
 * of ${a}, and return a vector consisting of that value copied to all lanes.
 */
static inline uint8x16_t
SubWord_duplicate(uint8x16_t a)
{

	/*
	 * Duplicate the final 32-bit word in all other lanes.  By having four
	 * copies of the same uint32_t, we cause the ShiftRows in the upcoming
	 * AESE to have no effect.
	 */
	a = vdupq_laneq_u32_u8(a, 3);

	/* AESE does AddRoundKey (nop), ShiftRows (nop), and SubBytes. */
	a = vaeseq_u8(a, vdupq_n_u8(0));

	return (a);
}

/**
 * SubWord_RotWord_XOR_duplicate(a, rcon):
 * Perform the AES key schedule operations of SubWord, RotWord, and XOR with
 * ${rcon}, acting on the final 32-bit word (bits 96..127) of ${a}, and return
 * a vector consisting of that value copied to all lanes.
 */
static inline uint8x16_t
SubWord_RotWord_XOR_duplicate(uint8x16_t a, const uint32_t rcon)
{
	uint32_t x3;

	/* Perform SubWord on the final 32-bit word and copy it to all lanes. */
	a = SubWord_duplicate(a);

	/* We'll use non-neon for the rest. */
	x3 = vgetq_lane_u32(vreinterpretq_u32_u8(a), 0);

	/*-
	 * x3 gets RotWord.  Note that
	 *     RotWord(SubWord(a)) == SubWord(RotWord(a))
	 */
	x3 = (x3 >> 8) | (x3 << (32 - 8));

	/* x3 gets XOR'd with rcon. */
	x3 = x3 ^ rcon;

	/* Copy x3 to all 128 bits, and convert it to a uint8x16_t. */
	return (vreinterpretq_u8_u32(vdupq_n_u32(x3)));
}

/* Compute an AES-128 round key. */
#define MKRKEY128(rkeys, i, rcon) do {				\
	uint8x16_t _s = rkeys[i - 1];				\
	uint8x16_t _t = rkeys[i - 1];				\
	_s = veorq_u8(_s, vshlq_n_u128(_s, 4));			\
	_s = veorq_u8(_s, vshlq_n_u128(_s, 8));			\
	_t = SubWord_RotWord_XOR_duplicate(_t, rcon);		\
	rkeys[i] = veorq_u8(_s, _t);				\
} while (0)

/**
 * crypto_aes_key_expand_128_arm(key, rkeys):
 * Expand the 128-bit AES key ${key} into the 11 round keys ${rkeys}.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
static void
crypto_aes_key_expand_128_arm(const uint8_t key[16], uint8x16_t rkeys[11])
{

	/* The first round key is just the key. */
	rkeys[0] = vld1q_u8(&key[0]);

	/*
	 * Each of the remaining round keys are computed from the preceding
	 * round key: rotword+subword+rcon (provided as aeskeygenassist) to
	 * compute the 'temp' value, then xor with 1, 2, 3, or all 4 of the
	 * 32-bit words from the preceding round key.
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
#define MKRKEY256(rkeys, i, rcon)	do {			\
	uint8x16_t _s = rkeys[i - 2];				\
	uint8x16_t _t = rkeys[i - 1];				\
	_s = veorq_u8(_s, vshlq_n_u128(_s, 4));			\
	_s = veorq_u8(_s, vshlq_n_u128(_s, 8));			\
	_t = (i % 2 == 1) ?					\
	    SubWord_duplicate(_t) :				\
	    SubWord_RotWord_XOR_duplicate(_t, rcon);		\
	rkeys[i] = veorq_u8(_s, _t);				\
} while (0)

/**
 * crypto_aes_key_expand_256_arm(key, rkeys):
 * Expand the 256-bit AES key ${key} into the 15 round keys ${rkeys}.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
static void
crypto_aes_key_expand_256_arm(const uint8_t key[32], uint8x16_t rkeys[15])
{

	/* The first two round keys are just the key. */
	rkeys[0] = vld1q_u8(&key[0]);
	rkeys[1] = vld1q_u8(&key[16]);

	/*
	 * Each of the remaining round keys are computed from the preceding
	 * pair of keys.  Even rounds use rotword+subword+rcon, while odd
	 * rounds just use subword.  The rcon value used is irrelevant for odd
	 * rounds since we ignore the value which it feeds into.
	 */
	MKRKEY256(rkeys, 2, 0x01);
	MKRKEY256(rkeys, 3, 0x00);
	MKRKEY256(rkeys, 4, 0x02);
	MKRKEY256(rkeys, 5, 0x00);
	MKRKEY256(rkeys, 6, 0x04);
	MKRKEY256(rkeys, 7, 0x00);
	MKRKEY256(rkeys, 8, 0x08);
	MKRKEY256(rkeys, 9, 0x00);
	MKRKEY256(rkeys, 10, 0x10);
	MKRKEY256(rkeys, 11, 0x00);
	MKRKEY256(rkeys, 12, 0x20);
	MKRKEY256(rkeys, 13, 0x00);
	MKRKEY256(rkeys, 14, 0x40);
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
	ALIGN_PTR_INIT(kexp->rkeys, sizeof(uint8x16_t));

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
 * crypto_aes_encrypt_block_arm_u8(in, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and return the
 * resulting ciphertext.  This implementation uses ARM AES instructions,
 * and should only be used if CPUSUPPORT_ARM_AES is defined and
 * cpusupport_arm_aes() returns nonzero.
 */
uint8x16_t
crypto_aes_encrypt_block_arm_u8(uint8x16_t in, const void * key)
{
	const struct crypto_aes_key_arm * _key = key;
	const uint8x16_t * aes_key = _key->rkeys;
	uint8x16_t aes_state = in;
	size_t nr = _key->nr;

	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[0]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[1]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[2]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[3]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[4]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[5]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[6]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[7]));
	aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[8]));
	if (nr > 10) {
		aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[9]));
		aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[10]));
		aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[11]));
		aes_state = vaesmcq_u8(vaeseq_u8(aes_state, aes_key[12]));
	}

	/* Last round. */
	aes_state = vaeseq_u8(aes_state, aes_key[nr - 1]);
	aes_state = veorq_u8(aes_state, aes_key[nr]);

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
	uint8x16_t aes_state;

	aes_state = vld1q_u8(in);
	aes_state = crypto_aes_encrypt_block_arm_u8(aes_state, key);
	vst1q_u8(out, aes_state);
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
