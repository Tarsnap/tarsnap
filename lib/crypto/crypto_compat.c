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
