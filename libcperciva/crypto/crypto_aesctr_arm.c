#include "cpusupport.h"
#ifdef CPUSUPPORT_ARM_AES
/**
 * CPUSUPPORT CFLAGS: ARM_AES
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "crypto_aes.h"
#include "crypto_aes_arm_u8.h"
#include "sysendian.h"

#include "crypto_aesctr_arm.h"

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
crypto_aesctr_arm_stream_wholeblocks(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen)
{
	uint8x16_t bufarm;
	uint8x16_t inbufarm;
	uint8x8_t nonce_be;
	uint8x8_t block_counter_be;
	uint8_t block_counter_be_arr[8];
	uint64_t block_counter;
	size_t num_blocks;
	size_t i;

	/* Load local variables from stream. */
	nonce_be = vld1_u8(stream->pblk);
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
		block_counter_be = vld1_u8(block_counter_be_arr);
		bufarm = vcombine_u8(nonce_be, block_counter_be);
		bufarm = crypto_aes_encrypt_block_arm_u8(bufarm, stream->key);

		/* Encrypt the byte(s). */
		inbufarm = vld1q_u8(*inbuf);
		bufarm = veorq_u8(inbufarm, bufarm);
		vst1q_u8(*outbuf, bufarm);

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
 * crypto_aesctr_arm_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream ${stream} and xor
 * them with bytes from ${inbuf}, writing the result into ${outbuf}.  If the
 * buffers ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void
crypto_aesctr_arm_stream(struct crypto_aesctr * stream, const uint8_t * inbuf,
    uint8_t * outbuf, size_t buflen)
{

	/* Process any bytes before we can process a whole block. */
	if (crypto_aesctr_stream_pre_wholeblock(stream, &inbuf, &outbuf,
	    &buflen))
		return;

	/* Process whole blocks of 16 bytes. */
	if (buflen >= 16)
		crypto_aesctr_arm_stream_wholeblocks(stream, &inbuf, &outbuf,
		    &buflen);

	/* Process any final bytes after finishing all whole blocks. */
	crypto_aesctr_stream_post_wholeblock(stream, &inbuf, &outbuf, &buflen);
}

#endif /* CPUSUPPORT_ARM_AES */
