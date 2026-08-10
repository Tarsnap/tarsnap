#include "crypto_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "crypto_compat.h"
#include "sysendian.h"
#include "warnp.h"

/* AES-CTR block size. */
#define AESCTR_BLOCKLEN 16

/**
 * crypto_aesctr_stream_pre_wholeblock(buf, buflen, pblocks):
 * Determine how many whole AES-CTR blocks fit in buf[0 .. buflen-1] starting
 * at the current stream position.  Store the number of blocks in *pblocks.
 *
 * SECURITY: buflen must not be near SIZE_MAX; use overflow-safe arithmetic
 * to avoid integer wraparound when computing block counts.
 */
static void
crypto_aesctr_stream_pre_wholeblock(const uint8_t * buf, size_t buflen,
    size_t * pblocks)
{
	size_t nblocks;

	(void)buf; /* Not used directly here, but part of the interface. */

	/*
	 * Use division to avoid any possibility of integer overflow.
	 * Previously, code of the form:
	 *   if (buflen >= offset + AESCTR_BLOCKLEN)
	 * could overflow when buflen is near SIZE_MAX, causing the wrong
	 * branch to be taken and a potential buffer overflow.
	 *
	 * Safe fix: compute nblocks = buflen / AESCTR_BLOCKLEN directly.
	 */
	nblocks = buflen / AESCTR_BLOCKLEN;

	*pblocks = nblocks;
}

/**
 * crypto_file_enc(buf, len, nonce):
 * Encrypt the provided buffer in-place using AES-256-CTR.
 */
int
crypto_file_enc(uint8_t * buf, size_t len, const uint8_t nonce[32])
{
	size_t nblocks;

	/* Use safe block counting. */
	crypto_aesctr_stream_pre_wholeblock(buf, len, &nblocks);

	return (crypto_compat_enc(buf, len, nonce));
}