#include <assert.h>

#include <openssl/rsa.h>

#include "crypto_compat.h"

/**
 * crypto_compat_RSA_valid_size(rsa):
 * Return nonzero if ${rsa} has a valid size, and zero for an invalid size.
 */
int
crypto_compat_RSA_valid_size(const RSA * const rsa)
{

	/* Sanity checks. */
	assert(rsa != NULL);
	assert(rsa->n != NULL);

	return ((RSA_size(rsa) == 256) && (BN_num_bits(rsa->n) == 2048));
}
