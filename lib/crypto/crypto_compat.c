#include <assert.h>

#include <openssl/rsa.h>

#include "crypto_compat.h"

#ifndef OPENSSL_VERSION_NUMBER
#error "OPENSSL_VERSION_NUMBER must be defined"
#endif

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

/**
 * crypto_compat_RSA_import(key, n, e, d, p, q, dmp1, dmq1, iqmp):
 * Import the given BIGNUMs into the RSA ${key}.
 */
int
crypto_compat_RSA_import(RSA ** key, BIGNUM * n, BIGNUM * e, BIGNUM * d,
    BIGNUM * p, BIGNUM * q, BIGNUM * dmp1, BIGNUM * dmq1, BIGNUM * iqmp)
{

	/* Sanity checks. */
	assert(key != NULL);
	assert((n != NULL) && (e != NULL));

	/* All the private-key-related variables are NULL, or they're not. */
	if (d == NULL) {
		assert((p == NULL) && (q == NULL) && (dmp1 == NULL)
		    && (dmq1 == NULL) && (iqmp == NULL));
	} else {
		assert((p != NULL) && (q != NULL) && (dmp1 != NULL)
		    && (dmq1 != NULL) && (iqmp != NULL));
	}

	/* Put values into RSA key. */
	(*key)->n = n;
	(*key)->e = e;
	if (d != NULL) {
		/* Private key. */
		(*key)->d = d;
		(*key)->p = p;
		(*key)->q = q;
		(*key)->dmp1 = dmp1;
		(*key)->dmq1 = dmq1;
		(*key)->iqmp = iqmp;
	}

	/* Success! */
	return (0);
}

/**
 * crypto_compat_RSA_export(key, n, e, d, p, q, dmp1, dmq1, iqmp):
 * Export values from the given RSA ${key} into the BIGNUMs.  ${n} and ${e}
 * must be non-NULL; the other values may be NULL if desired, and will
 * therefore not be exported.
 */
int
crypto_compat_RSA_export(RSA * key, const BIGNUM ** n, const BIGNUM ** e,
    const BIGNUM ** d, const BIGNUM ** p, const BIGNUM ** q,
    const BIGNUM ** dmp1, const BIGNUM ** dmq1, const BIGNUM ** iqmp)
{

	/* Sanity checks. */
	assert(key != NULL);
	assert((n != NULL) && (e != NULL));

	/* All the private-key-related variables are NULL, or they're not. */
	if (d == NULL) {
		assert((p == NULL) && (q == NULL) && (dmp1 == NULL)
		    && (dmq1 == NULL) && (iqmp == NULL));
	} else {
		assert((p != NULL) && (q != NULL) && (dmp1 != NULL)
		    && (dmq1 != NULL) && (iqmp != NULL));
	}

	/* Get values from RSA key. */
	*n = key->n;
	*e = key->e;
	if (d != NULL) {
		/* Private key. */
		*d = key->d;
		*p = key->p;
		*q = key->q;
		*dmp1 = key->dmp1;
		*dmq1 = key->dmq1;
		*iqmp = key->iqmp;
	}

	/* Success! */
	return (0);
}
