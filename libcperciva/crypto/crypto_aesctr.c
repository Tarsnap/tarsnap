#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "cpusupport.h"
#include "crypto_aes.h"
#include "crypto_aesctr_aesni.h"
#include "insecure_memzero.h"
#include "sysendian.h"

#include "crypto_aesctr.h"

/**
 * In order to optimize AES-CTR, it is desirable to separate out the handling
 * of individual bytes of data vs. the handling of complete (16 byte) blocks.
 * The handling of blocks in turn can be optimized further using CPU
 * intrinsics, e.g. SSE2 on x86 CPUs; however while the byte-at-once code
 * remains the same across platforms it should be inlined into the same (CPU
 * feature specific) routines for performance reasons.
 *
 * In order to allow those generic functions to be inlined into multiple
 * functions in separate translation units, we place them into a "shared" C
 * file which is included in each of the platform-specific variants.
 */
#include "crypto_aesctr_shared.c"

#if defined(CPUSUPPORT_X86_AESNI)
#define HWACCEL

static enum {
	HW_SOFTWARE = 0,
#if defined(CPUSUPPORT_X86_AESNI)
	HW_X86_AESNI,
#endif
	HW_UNSET
} hwaccel = HW_UNSET;
#endif

#ifdef HWACCEL
/* Which type of hardware acceleration should we use, if any? */
static void
hwaccel_init(void)
{

	/* If we've already set hwaccel, we're finished. */
	if (hwaccel != HW_UNSET)
		return;

	/* Default to software. */
	hwaccel = HW_SOFTWARE;

	/* Can we use AESNI? */
	if (crypto_aes_can_use_intrinsics() == 1)
		hwaccel = HW_X86_AESNI;
}
#endif /* HWACCEL */

/**
 * crypto_aesctr_alloc(void):
 * Allocate an object for performing AES in CTR code.  This must be followed
 * by calling _init2().
 */
struct crypto_aesctr *
crypto_aesctr_alloc(void)
{
	struct crypto_aesctr * stream;

	/* Allocate memory. */
	if ((stream = malloc(sizeof(struct crypto_aesctr))) == NULL)
		goto err0;

	/* Success! */
	return (stream);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_aesctr_init2(stream, key, nonce):
 * Reset the AES-CTR stream ${stream}, using the ${key} and ${nonce}.  If ${key}
 * is NULL, retain the previous AES key.
 */
void
crypto_aesctr_init2(struct crypto_aesctr * stream,
    const struct crypto_aes_key * key, uint64_t nonce)
{

	/* If the key is NULL, retain the previous AES key. */
	if (key != NULL)
		stream->key = key;

	/* Set nonce as provided and reset bytectr. */
	be64enc(stream->pblk, nonce);
	stream->bytectr = 0;

	/*
	 * Set the counter such that the least significant byte will wrap once
	 * incremented.
	 */
	stream->pblk[15] = 0xff;

#ifdef HWACCEL
	hwaccel_init();
#endif

	/* Sanity check. */
	assert(stream->key != NULL);
}

/**
 * crypto_aesctr_init(key, nonce):
 * Prepare to encrypt/decrypt data with AES in CTR mode, using the provided
 * expanded ${key} and ${nonce}.  The key provided must remain valid for the
 * lifetime of the stream.  This is the same as calling _alloc() followed by
 * _init2().
 */
struct crypto_aesctr *
crypto_aesctr_init(const struct crypto_aes_key * key, uint64_t nonce)
{
	struct crypto_aesctr * stream;

	/* Sanity check. */
	assert(key != NULL);

	/* Allocate memory. */
	if ((stream = crypto_aesctr_alloc()) == NULL)
		goto err0;

	/* Initialize values. */
	crypto_aesctr_init2(stream, key, nonce);

	/* Success! */
	return (stream);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_aesctr_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream ${stream} and xor
 * them with bytes from ${inbuf}, writing the result into ${outbuf}.  If the
 * buffers ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void
crypto_aesctr_stream(struct crypto_aesctr * stream, const uint8_t * inbuf,
    uint8_t * outbuf, size_t buflen)
{

#if defined(HWACCEL)
#if defined(CPUSUPPORT_X86_AESNI)
	if ((buflen >= 16) && (hwaccel == HW_X86_AESNI)) {
		crypto_aesctr_aesni_stream(stream, inbuf, outbuf, buflen);
		return;
	}
#endif
#endif /* HWACCEL */

	/* Process any bytes before we can process a whole block. */
	if (crypto_aesctr_stream_pre_wholeblock(stream, &inbuf, &outbuf,
	    &buflen))
		return;

	/* Process whole blocks of 16 bytes. */
	while (buflen >= 16) {
		/* Generate a block of cipherstream. */
		crypto_aesctr_stream_cipherblock_generate(stream);

		/* Encrypt the bytes and update the positions. */
		crypto_aesctr_stream_cipherblock_use(stream, &inbuf, &outbuf,
		    &buflen, 16, 0);
	}

	/* Process any final bytes after finishing all whole blocks. */
	crypto_aesctr_stream_post_wholeblock(stream, &inbuf, &outbuf, &buflen);
}

/**
 * crypto_aesctr_free(stream):
 * Free the AES-CTR stream ${stream}.
 */
void
crypto_aesctr_free(struct crypto_aesctr * stream)
{

	/* Behave consistently with free(NULL). */
	if (stream == NULL)
		return;

	/* Zero potentially sensitive information. */
	insecure_memzero(stream, sizeof(struct crypto_aesctr));

	/* Free the stream. */
	free(stream);
}

/**
 * crypto_aesctr_buf(key, nonce, inbuf, outbuf, buflen):
 * Equivalent to _init(key, nonce); _stream(inbuf, outbuf, buflen); _free().
 */
void
crypto_aesctr_buf(const struct crypto_aes_key * key, uint64_t nonce,
    const uint8_t * inbuf, uint8_t * outbuf, size_t buflen)
{
	struct crypto_aesctr stream_rec;
	struct crypto_aesctr * stream = &stream_rec;

	/* Sanity check. */
	assert(key != NULL);

	/* Initialize values. */
	crypto_aesctr_init2(stream, key, nonce);

	/* Perform the encryption. */
	crypto_aesctr_stream(stream, inbuf, outbuf, buflen);

	/* Zero potentially sensitive information. */
	insecure_memzero(stream, sizeof(struct crypto_aesctr));
}
