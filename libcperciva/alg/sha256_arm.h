#ifndef SHA256_ARM_H_
#define SHA256_ARM_H_

#include <stdint.h>

/**
 * SHA256_Transform_arm(state, block):
 * Compute the SHA256 block compression function, transforming ${state} using
 * the data in ${block}.  This implementation uses ARM SHA256 instructions,
 * and should only be used if _SHA256 is defined and cpusupport_arm_sha256()
 * returns nonzero.
 */
#ifdef POSIXFAIL_ABSTRACT_DECLARATOR
void SHA256_Transform_arm(uint32_t state[8], const uint8_t block[64]);
#else
void SHA256_Transform_arm(uint32_t[static restrict 8],
    const uint8_t[static restrict 64]);
#endif

#endif /* !SHA256_ARM_H_ */
