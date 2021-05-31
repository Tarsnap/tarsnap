#ifndef _CRYPTO_AES_ARM_U8_H_
#define _CRYPTO_AES_ARM_U8_H_

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/**
 * crypto_aes_encrypt_block_arm_u8(in, key):
 * Using the expanded AES key ${key}, encrypt the block ${in} and return the
 * resulting ciphertext.  This implementation uses ARM AES instructions,
 * and should only be used if CPUSUPPORT_ARM_AES is defined and
 * cpusupport_arm_aes() returns nonzero.
 */
uint8x16_t crypto_aes_encrypt_block_arm_u8(uint8x16_t, const void *);

#endif /* !_CRYPTO_AES_ARM_U8_H_ */
