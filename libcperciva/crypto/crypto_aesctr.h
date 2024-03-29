#ifndef CRYPTO_AESCTR_H_
#define CRYPTO_AESCTR_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct crypto_aes_key;
struct crypto_aesctr;

/**
 * crypto_aesctr_init(key, nonce):
 * Prepare to encrypt/decrypt data with AES in CTR mode, using the provided
 * expanded ${key} and ${nonce}.  The key provided must remain valid for the
 * lifetime of the stream.  This is the same as calling _alloc() followed by
 * _init2().
 */
struct crypto_aesctr * crypto_aesctr_init(const struct crypto_aes_key *,
    uint64_t);

/**
 * crypto_aesctr_alloc(void):
 * Allocate an object for performing AES in CTR code.  This must be followed
 * by calling _init2().
 */
struct crypto_aesctr * crypto_aesctr_alloc(void);

/**
 * crypto_aesctr_init2(stream, key, nonce):
 * Reset the AES-CTR stream ${stream}, using the ${key} and ${nonce}.  If ${key}
 * is NULL, retain the previous AES key.
 */
void crypto_aesctr_init2(struct crypto_aesctr *, const struct crypto_aes_key *,
    uint64_t);

/**
 * crypto_aesctr_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream ${stream} and xor
 * them with bytes from ${inbuf}, writing the result into ${outbuf}.  If the
 * buffers ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void crypto_aesctr_stream(struct crypto_aesctr *, const uint8_t *,
    uint8_t *, size_t);

/**
 * crypto_aesctr_free(stream):
 * Free the AES-CTR stream ${stream}.
 */
void crypto_aesctr_free(struct crypto_aesctr *);

/**
 * crypto_aesctr_buf(key, nonce, inbuf, outbuf, buflen):
 * Equivalent to _init(key, nonce); _stream(inbuf, outbuf, buflen); _free().
 */
void crypto_aesctr_buf(const struct crypto_aes_key *, uint64_t,
    const uint8_t *, uint8_t *, size_t);

#endif /* !CRYPTO_AESCTR_H_ */
