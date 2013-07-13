#include "bsdtar_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include "crypto_entropy.h"
#include "sysendian.h"
#include "warnp.h"

#include "crypto_internal.h"

/**
 * RSA private key data format:
 * n || e || d || p || q || (d mod (p-1)) || (d mod (q-1)) || (1/q mod p)
 * RSA public key data format:
 * n || e
 * All integers are stored in little-endian large integer format:
 * len || x[0] || x[1] ... x[len - 1]
 * where len is a 32-bit little-endian integer.
 */
/**
 * HMAC key data format:
 * x[0] || x[1] || x[2] ... x[31]
 */

static int import_BN(BIGNUM **, const uint8_t **, size_t *);
static int export_BN(BIGNUM *, uint8_t **, size_t *, uint32_t *);

/**
 * import_BN(bn, buf, buflen):
 * Import a large integer from the provided buffer, advance the buffer
 * pointer, and adjust the remaining buffer length.
 */
static int
import_BN(BIGNUM ** bn, const uint8_t ** buf, size_t * buflen)
{
	uint32_t len;
	uint8_t * bnbuf;
	size_t i;

	/* Parse integer length. */
	if (*buflen < sizeof(uint32_t)) {
		warn0("Unexpected EOF of key data");
		goto err0;
	}
	len = le32dec(*buf);
	*buf += sizeof(uint32_t);
	*buflen -= sizeof(uint32_t);

	/* Make sure there's enough data. */
	if (*buflen < len) {
		warn0("Unexpected EOF of key data");
		goto err0;
	}

	/*
	 * OpenSSL's BN_bin2bn wants input in big-endian format, so we need
	 * to use a temporary buffer to convert from le to be.
	 */
	if ((bnbuf = malloc(len)) == NULL)
		goto err0;
	for (i = 0; i < len; i++)
		bnbuf[len - 1 - i] = (*buf)[i];
	if ((*bn = BN_bin2bn(bnbuf, len, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err1;
	}
	free(bnbuf);

	/* Advance buffer pointer, adjust remaining buffer length. */
	*buf += len;
	*buflen -= len;

	/* Success! */
	return (0);

err1:
	free(bnbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * export_BN(bn, buf, buflen, len):
 * If ${*buf} != NULL, export the provided large integer into the buffer,
 * and adjust the buffer pointer and remaining buffer length appropriately.
 * Add the required storage length to ${len}.
 */
static int
export_BN(BIGNUM * bn, uint8_t ** buf, size_t * buflen,
    uint32_t * len)
{
	size_t i;
	unsigned int bnlen;

	/* Figure out how much space we need. */
	bnlen = BN_num_bytes(bn);

	/* Add the required storage length to ${len}. */
	if (*len + sizeof(uint32_t) < *len) {
		errno = ENOMEM;
		goto err0;
	}
	*len += sizeof(uint32_t);
	if (*len + bnlen < *len) {
		errno = ENOMEM;
		goto err0;
	}
	*len += bnlen;

	/* If ${*buf} == NULL, we're done. */
	if (*buf == NULL)
		goto done;

	/* Export the length of the integer. */
	if (*buflen < sizeof(uint32_t)) {
		warn0("Unexpected end of key buffer");
		goto err0;
	}
	le32enc(*buf, bnlen);
	*buf += sizeof(uint32_t);
	*buflen -= sizeof(uint32_t);

	/* Export the key as a big-endian integer. */
	if (*buflen < bnlen) {
		warn0("Unexpected end of key buffer");
		goto err0;
	}
	BN_bn2bin(bn, *buf);

	/* Convert to little-endian format. */
	for (i = 0; i < bnlen - 1 - i; i++) {
		(*buf)[i] ^= (*buf)[bnlen - 1 - i];
		(*buf)[bnlen - 1 - i] ^= (*buf)[i];
		(*buf)[i] ^= (*buf)[bnlen - 1 - i];
	}

	/* Adjust buffer pointer and remaining buffer length. */
	*buf += bnlen;
	*buflen -= bnlen;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_subr_import_RSA_priv(key, buf, buflen):
 * Import the specified RSA private key from the provided buffer.
 */
int
crypto_keys_subr_import_RSA_priv(RSA ** key, const uint8_t * buf, size_t buflen)
{

	/* Free any existing key. */
	if (*key != NULL)
		RSA_free(*key);

	/* Create a new key. */
	if ((*key = RSA_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Load values. */
	if (import_BN(&(*key)->n, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->e, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->d, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->p, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->q, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->dmp1, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->dmq1, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->iqmp, &buf, &buflen))
		goto err1;

	/* We should have no unprocessed data left. */
	if (buflen)
		goto err1;

	/* Success! */
	return (0);

err1:
	RSA_free(*key);
	*key = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_subr_import_RSA_pub(key, buf, buflen):
 * Import the specified RSA public key from the provided buffer.
 */
int
crypto_keys_subr_import_RSA_pub(RSA ** key, const uint8_t * buf, size_t buflen)
{

	/* Free any existing key. */
	if (*key != NULL)
		RSA_free(*key);

	/* Create a new key. */
	if ((*key = RSA_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Load values. */
	if (import_BN(&(*key)->n, &buf, &buflen))
		goto err1;
	if (import_BN(&(*key)->e, &buf, &buflen))
		goto err1;

	/* We should have no unprocessed data left. */
	if (buflen)
		goto err1;

	/* Success! */
	return (0);

err1:
	RSA_free(*key);
	*key = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_subr_import_HMAC(key, buf, buflen):
 * Import the specified HMAC key from the provided buffer.
 */
int
crypto_keys_subr_import_HMAC(struct crypto_hmac_key ** key,
    const uint8_t * buf, size_t buflen)
{

	/* Free any existing key. */
	if (*key != NULL) {
		free((*key)->key);
		free(*key);
	}

	/* Make sure the buffer is the right length. */
	if (buflen != 32) {
		warn0("Incorrect HMAC key size: %zu", buflen);
		goto err0;
	}

	/* Allocate key structure. */
	if ((*key = malloc(sizeof(struct crypto_hmac_key))) == NULL)
		goto err0;

	/* Allocate key buffer. */
	if (((*key)->key = malloc(buflen)) == NULL)
		goto err1;

	/* Copy key data and length. */
	(*key)->len = buflen;
	memcpy((*key)->key, buf, buflen);

	/* Success! */
	return (0);

err1:
	free(*key);
	*key = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_subr_export_RSA_priv(key, buf, buflen):
 * If buf != NULL, export the specified RSA private key.  Return the key
 * length in bytes.
 */
uint32_t
crypto_keys_subr_export_RSA_priv(RSA * key, uint8_t * buf, size_t buflen)
{
	uint32_t len = 0;

	if (key == NULL) {
		warn0("Cannot export a key which we don't have!");
		goto err0;
	}

	/* Each large integer gets exported. */
	if (export_BN(key->n, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->e, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->d, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->p, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->q, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->dmp1, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->dmq1, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->iqmp, &buf, &buflen, &len))
		goto err0;

	/* Success! */
	return (len);

err0:
	/* Failure! */
	return ((uint32_t)(-1));
}

/**
 * crypto_keys_subr_export_RSA_pub(key, buf, buflen):
 * If buf != NULL, export the specified RSA public key.  Return the key
 * length in bytes.
 */
uint32_t
crypto_keys_subr_export_RSA_pub(RSA * key, uint8_t * buf, size_t buflen)
{
	uint32_t len = 0;

	if (key == NULL) {
		warn0("Cannot export a key which we don't have!");
		goto err0;
	}

	/* Each large integer gets exported. */
	if (export_BN(key->n, &buf, &buflen, &len))
		goto err0;
	if (export_BN(key->e, &buf, &buflen, &len))
		goto err0;

	/* Success! */
	return (len);

err0:
	/* Failure! */
	return ((uint32_t)(-1));
}

/**
 * crypto_keys_subr_export_HMAC(key, buf, buflen):
 * If buf != NULL, export the specified HMAC key.  Return the key length
 * in bytes.
 */
uint32_t
crypto_keys_subr_export_HMAC(struct crypto_hmac_key * key, uint8_t * buf,
    size_t buflen)
{

	if (key == NULL) {
		warn0("Cannot export a key which we don't have!");
		goto err0;
	}

	if (buf != NULL) {
		if (buflen < key->len) {
			warn0("Unexpected end of key buffer");
			goto err0;
		}

		memcpy(buf, key->key, key->len);
	}

	/* Success! */
	return (key->len);

err0:
	/* Failure! */
	return ((uint32_t)(-1));
}

/**
 * crypto_keys_subr_generate_RSA(priv, pub):
 * Generate an RSA key and store the private and public parts.
 */
int
crypto_keys_subr_generate_RSA(RSA ** priv, RSA ** pub)
{

	/* Free any existing keys. */
	if (*priv != NULL)
		RSA_free(*priv);
	if (*pub != NULL)
		RSA_free(*pub);

	*priv = RSA_generate_key(2048, 65537, NULL, NULL);
	if (*priv == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	*pub = RSAPublicKey_dup(*priv);
	if (*pub == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err1;
	}

	/* Success! */
	return (0);

err1:
	RSA_free(*priv);
	*priv = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_subr_generate_HMAC(key):
 * Generate an HMAC key.
 */
int
crypto_keys_subr_generate_HMAC(struct crypto_hmac_key ** key)
{

	/* Free any existing key. */
	if (*key != NULL) {
		free((*key)->key);
		free(*key);
	}

	/* Allocate memory. */
	if ((*key = malloc(sizeof(struct crypto_hmac_key))) == NULL)
		goto err0;
	if (((*key)->key = malloc(32)) == NULL)
		goto err1;

	/* Store key length. */
	(*key)->len = 32;

	/* Generate key. */
	if (crypto_entropy_read((*key)->key, 32)) {
		warnp("Could not obtain sufficient entropy");
		goto err2;
	}

	/* Success! */
	return (0);

err2:
	free((*key)->key);
err1:
	free(*key);
err0:
	/* Failure! */
	return (-1);
}
