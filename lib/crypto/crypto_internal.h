#ifndef _CRYPTO_INTERNAL_H_
#define _CRYPTO_INTERNAL_H_

#include <stdint.h>

#include <openssl/aes.h>
#include <openssl/rsa.h>

struct crypto_hmac_key {
	size_t len;
	uint8_t * key;
};

/**
 * crypto_keys_lookup_RSA(key):
 * Return the requested RSA key.
 */
RSA * crypto_keys_lookup_RSA(int);

/**
 * crypto_keys_lookup_HMAC(key):
 * Return the requested HMAC key.
 */
struct crypto_hmac_key * crypto_keys_lookup_HMAC(int);

/**
 * crypto_keys_server_import_root(void):
 * Import the public part of the server root key.
 */
int crypto_keys_server_import_root(void);

/**
 * crypto_keys_subr_import_RSA_priv(key, buf, buflen):
 * Import the specified RSA private key from the provided buffer.
 */
int crypto_keys_subr_import_RSA_priv(RSA **, const uint8_t *, size_t);

/**
 * crypto_keys_subr_import_RSA_pub(key, buf, buflen):
 * Import the specified RSA public key from the provided buffer.
 */
int crypto_keys_subr_import_RSA_pub(RSA **, const uint8_t *, size_t);

/**
 * crypto_keys_subr_import_HMAC(key, buf, buflen):
 * Import the specified HMAC key from the provided buffer.
 */
int crypto_keys_subr_import_HMAC(struct crypto_hmac_key **, const uint8_t *,
    size_t);

/**
 * crypto_keys_subr_export_RSA_priv(key, buf, buflen):
 * If buf != NULL, export the specified RSA private key.  Return the key
 * length in bytes.
 */
uint32_t crypto_keys_subr_export_RSA_priv(RSA *, uint8_t *, size_t);

/**
 * crypto_keys_subr_export_RSA_pub(key, buf, buflen):
 * If buf != NULL, export the specified RSA public key.  Return the key
 * length in bytes.
 */
uint32_t crypto_keys_subr_export_RSA_pub(RSA *, uint8_t *, size_t);

/**
 * crypto_keys_subr_export_HMAC(key, buf, buflen):
 * If buf != NULL, export the specified HMAC key.  Return the key length
 * in bytes.
 */
uint32_t crypto_keys_subr_export_HMAC(struct crypto_hmac_key *, uint8_t *,
    size_t);

/**
 * crypto_keys_subr_generate_RSA(priv, pub):
 * Generate an RSA key and store the private and public parts.
 */
int crypto_keys_subr_generate_RSA(RSA **, RSA **);

/**
 * crypto_keys_subr_generate_HMAC(key):
 * Generate an HMAC key.
 */
int crypto_keys_subr_generate_HMAC(struct crypto_hmac_key **);

/**
 * crypto_file_init_keys(void):
 * Initialize the keys cached by crypto_file.
 */
int crypto_file_init_keys(void);

/**
 * crypto_MGF1(seed, seedlen, buf, buflen):
 * The MGF1 mask generation function, as specified in RFC 3447.
 */
void crypto_MGF1(uint8_t *, size_t, uint8_t *, size_t);

#endif /* !_CRYPTO_INTERNAL_H_ */
