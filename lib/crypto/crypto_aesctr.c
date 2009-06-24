/*-
 * Copyright 2007-2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "bsdtar_platform.h"

#include <stdint.h>
#include <stdlib.h>

#include <openssl/aes.h>

#include "sysendian.h"

#include "crypto_internal.h"

struct crypto_aesctr {
	AES_KEY * key;
	uint64_t nonce;
	uint64_t bytectr;
	uint8_t buf[16];
};

/**
 * crypto_aesctr_init(key, nonce):
 * Prepare to encrypt/decrypt data with AES in CTR mode, using the provided
 * expanded key and nonce.  The key provided must remain valid for the
 * lifetime of the stream.
 */
struct crypto_aesctr *
crypto_aesctr_init(AES_KEY * key, uint64_t nonce)
{
	struct crypto_aesctr * stream;

	/* Allocate memory. */
	if ((stream = malloc(sizeof(struct crypto_aesctr))) == NULL)
		goto err0;

	/* Initialize values. */
	stream->key = key;
	stream->nonce = nonce;
	stream->bytectr = 0;

	/* Success! */
	return (stream);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_aesctr_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream and xor them with
 * bytes from ${inbuf}, writing the result into ${outbuf}.  If the buffers
 * ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void
crypto_aesctr_stream(struct crypto_aesctr * stream, const uint8_t * inbuf,
    uint8_t * outbuf, size_t buflen)
{
	uint8_t pblk[16];
	size_t pos;
	int bytemod;

	for (pos = 0; pos < buflen; pos++) {
		/* How far through the buffer are we? */
		bytemod = stream->bytectr % 16;

		/* Generate a block of cipherstream if needed. */
		if (bytemod == 0) {
			be64enc(pblk, stream->nonce);
			be64enc(pblk + 8, stream->bytectr / 16);
			AES_encrypt(pblk, stream->buf, stream->key);
		}

		/* Encrypt a byte. */
		outbuf[pos] = inbuf[pos] ^ stream->buf[bytemod];

		/* Move to the next byte of cipherstream. */
		stream->bytectr += 1;
	}
}

/**
 * crypto_aesctr_free(stream):
 * Free the provided stream object.
 */
void
crypto_aesctr_free(struct crypto_aesctr * stream)
{
	int i;

	/* Zero potentially sensitive information. */
	for (i = 0; i < 16; i++)
		stream->buf[i] = 0;
	stream->bytectr = stream->nonce = 0;

	/* Free the stream. */
	free(stream);
}
