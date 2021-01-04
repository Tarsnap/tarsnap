#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_AESNI

#include <assert.h>
#include <stdint.h>

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

#endif /* CPUSUPPORT_X86_AESNI */
