#ifndef CRYPTO_AESCTR_ARM_H_
#define CRYPTO_AESCTR_ARM_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque type. */
struct crypto_aesctr;

/**
 * crypto_aesctr_arm_stream(stream, inbuf, outbuf, buflen):
 * Generate the next ${buflen} bytes of the AES-CTR stream ${stream} and xor
 * them with bytes from ${inbuf}, writing the result into ${outbuf}.  If the
 * buffers ${inbuf} and ${outbuf} overlap, they must be identical.
 */
void crypto_aesctr_arm_stream(struct crypto_aesctr *, const uint8_t *,
    uint8_t *, size_t);

#endif /* !CRYPTO_AESCTR_ARM_H_ */
