#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_AESNI

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

/* Process multiple whole blocks by generating & using a cipherblock. */
static void
crypto_aesctr_aesni_stream_wholeblocks(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen)
{
	__m128i bufsse;
	__m128i inbufsse;
	__m128i nonce_be;
	uint8_t block_counter_be_arr[8];
	size_t num_blocks;
	size_t i;

	/* Load local variables from stream. */
	nonce_be = _mm_loadu_si64(stream->pblk);
	memcpy(block_counter_be_arr, stream->pblk + 8, 8);

	/* How many blocks should we process? */
	num_blocks = (*buflen) / 16;

	for (i = 0; i < num_blocks; i++) {
		/* Prepare counter. */
		block_counter_be_arr[7]++;
		if (block_counter_be_arr[7] == 0) {
			/*
			 * If incrementing the least significant byte resulted
			 * in it wrapping, re-encode the complete 64-bit
			 * value.
			 */
			be64enc(block_counter_be_arr, stream->bytectr / 16);
		}

		/* Encrypt the cipherblock. */
		bufsse = _mm_loadu_si64(block_counter_be_arr);
		bufsse = _mm_unpacklo_epi64(nonce_be, bufsse);
		bufsse = crypto_aes_encrypt_block_aesni_m128i(bufsse,
		    stream->key);

		/* Encrypt the byte(s). */
		inbufsse = _mm_loadu_si128((const __m128i *)(*inbuf));
		bufsse = _mm_xor_si128(inbufsse, bufsse);
		_mm_storeu_si128((__m128i *)(*outbuf), bufsse);

		/* Update the positions. */
		stream->bytectr += 16;
		*inbuf += 16;
		*outbuf += 16;
	}

	/* Update the overall buffer length. */
	*buflen -= 16 * num_blocks;

	/* Update variables in stream. */
	memcpy(stream->pblk + 8, block_counter_be_arr, 8);
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
