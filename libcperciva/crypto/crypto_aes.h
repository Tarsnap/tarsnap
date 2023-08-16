#ifndef CRYPTO_AES_H_
#define CRYPTO_AES_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque structure. */
struct crypto_aes_key;

/**
 * crypto_aes_can_use_intrinsics(void):
 * Test whether hardware intrinsics are safe to use.  Return 1 if x86 AESNI
 * operations are available, 2 if ARM-AES operations are available, or 0 if
 * none are available.
 */
int crypto_aes_can_use_intrinsics(void);

/**
 * crypto_aes_key_expand(key_unexpanded, len):
 * Expand the ${len}-byte unexpanded AES key ${key_unexpanded} into a
 * structure which can be passed to crypto_aes_encrypt_block().  The length
 * must be 16 or 32.
 */
struct crypto_aes_key * crypto_aes_key_expand(const uint8_t *, size_t);

/**
 * crypto_aes_encrypt_block(in, out, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and write the
 * resulting ciphertext to ${out}.  ${in} and ${out} can overlap.
 */
void crypto_aes_encrypt_block(const uint8_t[16], uint8_t[16],
    const struct crypto_aes_key *);

/**
 * crypto_aes_key_free(key):
 * Free the expanded AES key ${key}.
 */
void crypto_aes_key_free(struct crypto_aes_key *);

#endif /* !CRYPTO_AES_H_ */
