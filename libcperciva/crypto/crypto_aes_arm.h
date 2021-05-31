#ifndef _CRYPTO_AES_ARM_H_
#define _CRYPTO_AES_ARM_H_

#include <stddef.h>
#include <stdint.h>

/**
 * crypto_aes_key_expand_arm(key, len):
 * Expand the ${len}-byte AES key ${key} into a structure which can be passed
 * to crypto_aes_encrypt_block_arm().  The length must be 16 or 32.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
void * crypto_aes_key_expand_arm(const uint8_t *, size_t);

/**
 * crypto_aes_encrypt_block_arm(in, out, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and write the
 * resulting ciphertext to ${out}.  ${in} and ${out} can overlap.  This
 * implementation uses ARM AES instructions, and should only be used if
 * CPUSUPPORT_ARM_AES is defined and cpusupport_arm_aes() returns nonzero.
 */
void crypto_aes_encrypt_block_arm(const uint8_t[16], uint8_t[16],
    const void *);

/**
 * crypto_aes_key_free_arm(key):
 * Free the expanded AES key ${key}.
 */
void crypto_aes_key_free_arm(void *);

#endif /* !_CRYPTO_AES_ARM_H_ */
