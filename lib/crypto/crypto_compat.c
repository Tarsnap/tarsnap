#include <assert.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rsa.h>

#include "warnp.h"

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

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	assert(rsa->n != NULL);
	return ((RSA_size(rsa) == 256) && (BN_num_bits(rsa->n) == 2048));
#else
	return ((RSA_size(rsa) == 256) && (RSA_bits(rsa) == 2048));
#endif
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
#if OPENSSL_VERSION_NUMBER < 0x10100000L
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
#else
	/* Do we have a public key, or private key? */
	if (d == NULL) {
		/* We could use d here, but using NULL makes it more clear. */
		if (RSA_set0_key(*key, n, e, NULL) != 1)
			goto err0;
	} else {
		/* Private key. */
		if (RSA_set0_key(*key, n, e, d) != 1)
			goto err0;
		if (RSA_set0_factors(*key, p, q) != 1)
			goto err0;
		if (RSA_set0_crt_params(*key, dmp1, dmq1, iqmp) != 1)
			goto err0;
	}
#endif

	/* Success! */
	return (0);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#else
err0:
	/* Failure! */
	return (-1);
#endif
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
#if OPENSSL_VERSION_NUMBER < 0x10100000L
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
#else
	/* Do we have a public key, or private key? */
	if (d == NULL) {
		/* We could use d here, but using NULL makes it more clear. */
		RSA_get0_key(key, n, e, NULL);
	} else {
		/* Private key. */
		RSA_get0_key(key, n, e, d);
		RSA_get0_factors(key, p, q);
		RSA_get0_crt_params(key, dmp1, dmq1, iqmp);
	}
#endif

	/* Success! */
	return (0);
}

/**
 * crypto_compat_RSA_generate_key():
 * Generate a key pair.
 */
RSA *
crypto_compat_RSA_generate_key()
{
	RSA * key;

#if OPENSSL_VERSION_NUMBER < 0x00908000L
	/* Generate key. */
	if ((key = RSA_generate_key(2048, 65537, NULL, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Success! */
	return (key);
#else
	BIGNUM * e;

	/* Set up parameter. */
	if ((e = BN_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}
	BN_set_word(e, 65537);

	/* Generate key. */
	if ((key = RSA_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err1;
	}
	if (RSA_generate_key_ex(key, 2048, e, NULL) != 1) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err2;
	}

	/* Clean up. */
	BN_free(e);

	/* Success! */
	return (key);

err2:
	RSA_free(key);
err1:
	BN_free(e);
#endif
err0:
	/* Failure! */
	return (NULL);
}
