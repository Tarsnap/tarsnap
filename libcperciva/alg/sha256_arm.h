#ifndef _SHA256_ARM_H_
#define _SHA256_ARM_H_

#include <stdint.h>

/**
 * SHA256_Transform_arm(state, block, W, S):
 * Compute the SHA256 block compression function, transforming ${state} using
 * the data in ${block}.  This implementation uses ARM SHA256 instructions,
 * and should only be used if _SHA256 is defined and cpusupport_arm_sha256()
 * returns nonzero.  The arrays W and S may be filled with sensitive data, and
 * should be cleared by the callee.
 */
#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
void SHA256_Transform_arm(uint32_t state[8],
    const uint8_t block[64], uint32_t W[64], uint32_t S[8]);
#else
void SHA256_Transform_arm(uint32_t[static restrict 8],
    const uint8_t[static restrict 64], uint32_t W[static restrict 64],
    uint32_t S[static restrict 8]);
#endif

#endif /* !_SHA256_ARM_H_ */
