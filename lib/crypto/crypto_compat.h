#ifndef _CRYPTO_COMPAT_H_
#define _CRYPTO_COMPAT_H_

/**
 * crypto_compat_RSA_valid_size(rsa):
 * Return nonzero if ${rsa} has a valid size, and zero for an invalid size.
 */
int crypto_compat_RSA_valid_size(const RSA * const key);

#endif
