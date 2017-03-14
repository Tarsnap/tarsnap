#ifndef _CRYPTO_COMPAT_H_
#define _CRYPTO_COMPAT_H_

/**
 * crypto_compat_RSA_valid_size(rsa):
 * Return nonzero if ${rsa} has a valid size, and zero for an invalid size.
 */
int crypto_compat_RSA_valid_size(const RSA * const key);

/**
 * crypto_compat_RSA_import(key, n, e, d, p, q, dmp1, dmq1, iqmp):
 * Import the given BIGNUMs into the RSA ${key}.
 */
int crypto_compat_RSA_import(RSA ** key, BIGNUM * n, BIGNUM * e, BIGNUM * d,
    BIGNUM * p, BIGNUM * q, BIGNUM * dmp1, BIGNUM * dmq1, BIGNUM * iqmp);

#endif
