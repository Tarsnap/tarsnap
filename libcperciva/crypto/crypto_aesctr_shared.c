/*
 * This code is shared between crypto_aesctr*.c files, and should not be
 * compiled as a separate translation unit.  For details, see the comments in
 * those files.
 */

/* AES-CTR state. */
struct crypto_aesctr {
	const struct crypto_aes_key * key;
	uint64_t bytectr;
	uint8_t buf[16];
	uint8_t pblk[16];
};

/* Generate a block of cipherstream. */
static inline void
crypto_aesctr_stream_cipherblock_generate(struct crypto_aesctr * stream)
{

	/* Sanity check. */
	assert(stream->bytectr % 16 == 0);

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
}

/* Encrypt ${nbytes} bytes, then update ${inbuf}, ${outbuf}, and ${buflen}. */
static inline void
crypto_aesctr_stream_cipherblock_use(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen, size_t nbytes,
    size_t bytemod)
{
	size_t i;

	/* Encrypt the byte(s). */
	for (i = 0; i < nbytes; i++)
		(*outbuf)[i] = (*inbuf)[i] ^ stream->buf[bytemod + i];

	/* Move to the next byte(s) of cipherstream. */
	stream->bytectr += nbytes;

	/* Update the positions. */
	*inbuf += nbytes;
	*outbuf += nbytes;
	*buflen -= nbytes;
}

/*
 * Process any bytes before we can process a whole block.  Return 1 if there
 * are no bytes left to process after calling this function.
 */
static inline int
crypto_aesctr_stream_pre_wholeblock(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen_p)
{
	size_t bytemod;

	/* Do we have any bytes left in the current cipherblock? */
	bytemod = stream->bytectr % 16;
	if (bytemod != 0) {
		/* Do we have enough to complete the request? */
		if (bytemod + *buflen_p <= 16) {
			/* Process only buflen bytes, then return. */
			crypto_aesctr_stream_cipherblock_use(stream, inbuf,
			    outbuf, buflen_p, *buflen_p, bytemod);
			return (1);
		}

		/* Encrypt the byte(s) and update the positions. */
		crypto_aesctr_stream_cipherblock_use(stream, inbuf, outbuf,
		    buflen_p, 16 - bytemod, bytemod);
	}

	return (0);
}

/* Process any final bytes after finishing all whole blocks. */
static inline void
crypto_aesctr_stream_post_wholeblock(struct crypto_aesctr * stream,
    const uint8_t ** inbuf, uint8_t ** outbuf, size_t * buflen_p)
{

	/* Process any final bytes; we need a new cipherblock. */
	if (*buflen_p > 0) {
		/* Generate a block of cipherstream. */
		crypto_aesctr_stream_cipherblock_generate(stream);

		/* Encrypt the byte(s) and update the positions. */
		crypto_aesctr_stream_cipherblock_use(stream, inbuf, outbuf,
		    buflen_p, *buflen_p, 0);
	}
}
