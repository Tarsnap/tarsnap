/**
 * APISUPPORT CFLAGS: LIBCRYPTO_LOW_LEVEL_AES
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/aes.h>

#include "cpusupport.h"
#include "crypto_aes_aesni.h"
#include "crypto_aes_arm.h"
#include "insecure_memzero.h"
#include "warnp.h"

#include "crypto_aes.h"

#if defined(CPUSUPPORT_X86_AESNI) || defined(CPUSUPPORT_ARM_AES)
#define HWACCEL

static enum {
	HW_SOFTWARE = 0,
#if defined(CPUSUPPORT_X86_AESNI)
	HW_X86_AESNI,
#endif
#if defined(CPUSUPPORT_ARM_AES)
	HW_ARM_AES,
#endif
	HW_UNSET
} hwaccel = HW_UNSET;
#endif

/**
 * This represents either an AES_KEY or a struct crypto_aes_key_aesni; we
 * know which it is based on whether we're using AESNI code or not.  As such,
 * it's just an opaque pointer; but declaring it as a named structure type
 * prevents type-mismatch bugs in upstream code.
 */
struct crypto_aes_key;

#ifdef HWACCEL
static struct aes_test {
	const uint8_t key[32];
	const size_t len;
	const uint8_t ptext[16];
	const uint8_t ctext[16];
} testcases[] = { {
	/* NIST FIPS 179, Appendix C - Example Vectors, AES-128, p. 35. */
	.key = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f},
	.len = 16,
	.ptext = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		   0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff },
	.ctext = { 0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
		   0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a }
	}, {
	/* NIST FIPS 179, Appendix C - Example Vectors, AES-256, p. 42. */
	.key = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, },
	.len = 32,
	.ptext = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		   0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff },
	.ctext = { 0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
		   0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89 }
	}
};

/* Test a function against test vectors. */
static int
functest(int (* func)(const uint8_t *, size_t, const uint8_t[16], uint8_t[16]))
{
	struct aes_test * knowngood;
	uint8_t ctext[16];
	size_t i;

	for (i = 0; i < sizeof(testcases) / sizeof(testcases[0]); i++) {
		knowngood = &testcases[i];

		/* Sanity-check. */
		assert((knowngood->len == 16) || (knowngood->len == 32));

		/* Expand the key and encrypt with the provided function. */
		if (func(knowngood->key, knowngood->len, knowngood->ptext,
		    ctext))
			goto err0;

		/* Does the output match the known good value? */
		if (memcmp(knowngood->ctext, ctext, 16))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

#if defined(CPUSUPPORT_X86_AESNI)
static int
x86_aesni_oneshot(const uint8_t * key_unexpanded, size_t len,
    const uint8_t ptext[16], uint8_t ctext[16])
{
	void * kexp_hw;

	/* Expand the key and encrypt with hardware intrinsics. */
	if ((kexp_hw = crypto_aes_key_expand_aesni(key_unexpanded, len))
	    == NULL)
		goto err0;
	crypto_aes_encrypt_block_aesni(ptext, ctext, kexp_hw);
	crypto_aes_key_free_aesni(kexp_hw);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
#endif

#if defined(CPUSUPPORT_ARM_AES)
static int
arm_aes_oneshot(const uint8_t * key_unexpanded, size_t len,
    const uint8_t ptext[16], uint8_t * ctext)
{
	void * kexp_hw;

	if ((kexp_hw = crypto_aes_key_expand_arm(key_unexpanded, len)) == NULL)
		goto err0;
	crypto_aes_encrypt_block_arm(ptext, ctext, kexp_hw);
	crypto_aes_key_free_arm(kexp_hw);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
#endif

static int
openssl_oneshot(const uint8_t * key_unexpanded, size_t len,
    const uint8_t ptext[16], uint8_t * ctext)
{
	AES_KEY kexp_actual;
	AES_KEY * kexp = &kexp_actual;

	/* Expand the key, encrypt, and clean up. */
	if (AES_set_encrypt_key(key_unexpanded, (int)(len * 8), kexp) != 0)
		goto err0;
	AES_encrypt(ptext, ctext, kexp);
	insecure_memzero(kexp, sizeof(AES_KEY));

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Which type of hardware acceleration should we use, if any? */
static void
hwaccel_init(void)
{

	/* If we've already set hwaccel, we're finished. */
	if (hwaccel != HW_UNSET)
		return;

	/* Default to software. */
	hwaccel = HW_SOFTWARE;

#if defined(CPUSUPPORT_X86_AESNI)
	CPUSUPPORT_VALIDATE(hwaccel, HW_X86_AESNI, cpusupport_x86_aesni(),
	    functest(x86_aesni_oneshot));
#endif
#if defined(CPUSUPPORT_ARM_AES)
	CPUSUPPORT_VALIDATE(hwaccel, HW_ARM_AES, cpusupport_arm_aes(),
	    functest(arm_aes_oneshot));
#endif

	/*
	 * If we're here, we're not using any intrinsics.  Test OpenSSL; if
	 * there's an error, print a warning and abort.
	 */
	if (functest(openssl_oneshot)) {
		warn0("OpenSSL gives incorrect AES values.");
		abort();
	}
}
#endif /* HWACCEL */

/**
 * crypto_aes_can_use_intrinsics(void):
 * Test whether hardware intrinsics are safe to use.  Return 1 if x86 AESNI
 * operations are available, 2 if ARM-AES operations are available, or 0 if
 * none are available.
 */
int
crypto_aes_can_use_intrinsics(void)
{

#ifdef HWACCEL
	/* Ensure that we've chosen the type of hardware acceleration. */
	hwaccel_init();

#if defined(CPUSUPPORT_X86_AESNI)
	if (hwaccel == HW_X86_AESNI)
		return (1);
#endif
#if defined(CPUSUPPORT_ARM_AES)
	if (hwaccel == HW_ARM_AES)
		return (2);
#endif
#endif /* HWACCEL */

	/* Software only. */
	return (0);
}

/**
 * crypto_aes_key_expand(key_unexpanded, len):
 * Expand the ${len}-byte unexpanded AES key ${key_unexpanded} into a
 * structure which can be passed to crypto_aes_encrypt_block().  The length
 * must be 16 or 32.
 */
struct crypto_aes_key *
crypto_aes_key_expand(const uint8_t * key_unexpanded, size_t len)
{
	AES_KEY * kexp;

	/* Sanity-check. */
	assert((len == 16) || (len == 32));

#ifdef HWACCEL
	/* Ensure that we've chosen the type of hardware acceleration. */
	hwaccel_init();

#ifdef CPUSUPPORT_X86_AESNI
	if (hwaccel == HW_X86_AESNI)
		return (crypto_aes_key_expand_aesni(key_unexpanded, len));
#endif
#ifdef CPUSUPPORT_ARM_AES
	if (hwaccel == HW_ARM_AES)
		return (crypto_aes_key_expand_arm(key_unexpanded, len));
#endif
#endif /* HWACCEL */

	/* Allocate structure. */
	if ((kexp = malloc(sizeof(AES_KEY))) == NULL)
		goto err0;

	/* Expand the key. */
	if (AES_set_encrypt_key(key_unexpanded, (int)(len * 8), kexp) != 0)
		goto err1;

	/* Success! */
	return ((void *)kexp);

err1:
	free(kexp);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_aes_encrypt_block(in, out, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and write the
 * resulting ciphertext to ${out}.  ${in} and ${out} can overlap.
 */
void
crypto_aes_encrypt_block(const uint8_t in[16], uint8_t out[16],
    const struct crypto_aes_key * key)
{

#ifdef HWACCEL
#ifdef CPUSUPPORT_X86_AESNI
	if (hwaccel == HW_X86_AESNI) {
		crypto_aes_encrypt_block_aesni(in, out, (const void *)key);
		return;
	}
#endif
#ifdef CPUSUPPORT_ARM_AES
	if (hwaccel == HW_ARM_AES) {
		crypto_aes_encrypt_block_arm(in, out, (const void *)key);
		return;
	}
#endif
#endif /* HWACCEL */

	/* Get AES to do the work. */
	AES_encrypt(in, out, (const void *)key);
}

/**
 * crypto_aes_key_free(key):
 * Free the expanded AES key ${key}.
 */
void
crypto_aes_key_free(struct crypto_aes_key * key)
{

#ifdef HWACCEL
#ifdef CPUSUPPORT_X86_AESNI
	if (hwaccel == HW_X86_AESNI) {
		crypto_aes_key_free_aesni((void *)key);
		return;
	}
#endif
#ifdef CPUSUPPORT_ARM_AES
	if (hwaccel == HW_ARM_AES) {
		crypto_aes_key_free_arm((void *)key);
		return;
	}
#endif
#endif /* HWACCEL */

	/* Behave consistently with free(NULL). */
	if (key == NULL)
		return;

	/* Attempt to zero the expanded key. */
	insecure_memzero(key, sizeof(AES_KEY));

	/* Free the key. */
	free(key);
}
