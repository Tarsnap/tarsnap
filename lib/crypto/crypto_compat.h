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

/**
 * crypto_compat_RSA_export(key, n, e, d, p, q, dmp1, dmq1, iqmp):
 * Export values from the given RSA ${key} into the BIGNUMs.  ${n} and ${e}
 * must be non-NULL; the other values may be NULL if desired, and will
 * therefore not be exported.
 */
int crypto_compat_RSA_export(RSA * key, const BIGNUM ** n, const BIGNUM ** e,
    const BIGNUM ** d, const BIGNUM ** p, const BIGNUM ** q,
    const BIGNUM ** dmp1, const BIGNUM ** dmq1, const BIGNUM ** iqmp);

/**
 * crypto_compat_RSA_generate_key():
 * Generate a key pair.
 */
RSA * crypto_compat_RSA_generate_key(void);

#endif
