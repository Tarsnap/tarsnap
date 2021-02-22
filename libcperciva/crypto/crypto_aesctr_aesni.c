#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_AESNI
/**
 * CPUSUPPORT CFLAGS: X86_AESNI
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <emmintrin.h>

#include "crypto_aes.h"
#include "crypto_aes_aesni_m128i.h"
#include "sysendian.h"

#include "crypto_aesctr_aesni.h"

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

/**
 * load_si64(mem):
 * Load an unaligned 64-bit integer from memory into the lowest 64 bits of the
 * returned value.  The contents of the upper 64 bits is not defined.
 */
static inline __m128i
load_si64(const void * mem)
{

	return (_mm_loadu_si64(mem));
}

/* Process multiple whole blocks by generating & using a cipherblock. */
static void
crypto_aesctr_aesni_stream_wholeblocks(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen)
{
	__m128i bufsse;
	__m128i inbufsse;
	__m128i nonce_be;
	uint8_t block_counter_be_arr[8];
	uint64_t block_counter;
	size_t num_blocks;
	size_t i;

	/* Load local variables from stream. */
	nonce_be = load_si64(stream->pblk);
	block_counter = stream->bytectr / 16;

	/* How many blocks should we process? */
	num_blocks = (*buflen) / 16;

	/*
	 * This is 'for (i = num_blocks; i > 0; i--)', but ensuring that the
	 * compiler knows that we will execute the loop at least once.
	 */
	i = num_blocks;
	do {
		/* Prepare counter. */
		be64enc(block_counter_be_arr, block_counter);

		/* Encrypt the cipherblock. */
		bufsse = load_si64(block_counter_be_arr);
		bufsse = _mm_unpacklo_epi64(nonce_be, bufsse);
		bufsse = crypto_aes_encrypt_block_aesni_m128i(bufsse,
		    stream->key);

		/* Encrypt the byte(s). */
		inbufsse = _mm_loadu_si128((const __m128i *)(*inbuf));
		bufsse = _mm_xor_si128(inbufsse, bufsse);
		_mm_storeu_si128((__m128i *)(*outbuf), bufsse);

		/* Update the positions. */
		block_counter++;
		*inbuf += 16;
		*outbuf += 16;

		/* Update the counter. */
		i--;
	} while (i > 0);

	/* Update the overall buffer length. */
	*buflen -= 16 * num_blocks;

	/* Update variables in stream. */
	memcpy(stream->pblk + 8, block_counter_be_arr, 8);
	stream->bytectr += 16 * num_blocks;
}

/**
 * crypto_aesctr_aesni_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream ${stream} and xor
 * them with bytes from ${inbuf}, writing the result into ${outbuf}.  If the
 * buffers ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void
crypto_aesctr_aesni_stream(struct crypto_aesctr * stream, const uint8_t * inbuf,
    uint8_t * outbuf, size_t buflen)
{

	/* Process any bytes before we can process a whole block. */
	if (crypto_aesctr_stream_pre_wholeblock(stream, &inbuf, &outbuf,
	    &buflen))
		return;

	/* Process whole blocks of 16 bytes. */
	if (buflen >= 16)
		crypto_aesctr_aesni_stream_wholeblocks(stream, &inbuf,
		    &outbuf, &buflen);

	/* Process any final bytes after finishing all whole blocks. */
	crypto_aesctr_stream_post_wholeblock(stream, &inbuf, &outbuf, &buflen);
}

#endif /* CPUSUPPORT_X86_AESNI */
