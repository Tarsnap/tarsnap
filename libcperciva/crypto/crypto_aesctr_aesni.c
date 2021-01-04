#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_AESNI

#include <assert.h>
#include <stdint.h>

#include <emmintrin.h>

#include "crypto_aes.h"
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

/* Process one whole block of generating & using a cipherblock. */
static void
crypto_aesctr_aesni_stream_wholeblock(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen_p)
{
	__m128i bufsse;
	__m128i inbufsse;

	/* Prepare counter. */
	stream->pblk[15]++;
	if (stream->pblk[15] == 0) {
		/*
		 * If incrementing the least significant byte resulted in it
		 * wrapping, re-encode the complete 64-bit value.
		 */
		be64enc(stream->pblk + 8, stream->bytectr / 16);
	}

	/* Encrypt the cipherblock. */
	crypto_aes_encrypt_block(stream->pblk, stream->buf, stream->key);
	bufsse = _mm_loadu_si128((const __m128i *)stream->buf);

	/* Encrypt the byte(s). */
	inbufsse = _mm_loadu_si128((const __m128i *)(*inbuf));
	bufsse = _mm_xor_si128(inbufsse, bufsse);
	_mm_storeu_si128((__m128i *)(*outbuf), bufsse);

	/* Update the positions. */
	stream->bytectr += 16;
	*inbuf += 16;
	*outbuf += 16;
	*buflen_p -= 16;
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
	while (buflen >= 16)
		crypto_aesctr_aesni_stream_wholeblock(stream, &inbuf, &outbuf,
		    &buflen);

	/* Process any final bytes after finishing all whole blocks. */
	crypto_aesctr_stream_post_wholeblock(stream, &inbuf, &outbuf, &buflen);

}

#endif /* CPUSUPPORT_X86_AESNI */
