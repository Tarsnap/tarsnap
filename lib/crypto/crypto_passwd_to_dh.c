#include "bsdtar_platform.h"

#include <string.h>

#include "crypto_dh.h"
#include "ctassert.h"

#include "crypto.h"

/* We use HMAC-SHA256 to generate a DH private key; so the size must match. */
CTASSERT(CRYPTO_DH_PRIVLEN == 32);

/**
 * crypto_passwd_to_dh(passwd, salt, pub, priv):
 * Generate a Diffie-Hellman pair (${priv}, ${pub}), with ${pub} equal to
 * 2^(2^258 + ${priv}) modulo the group #14 modulus, and ${priv} equal to
 * HMAC(${salt}, ${passwd}), where ${passwd} is a NUL-terminated string.
 */
int
crypto_passwd_to_dh(const char * passwd, const uint8_t salt[32],
    uint8_t pub[CRYPTO_DH_PUBLEN], uint8_t priv[CRYPTO_DH_PRIVLEN])
{

	/* Generate private key via HMAC. */
	crypto_hash_data_key(salt, 32,
	    (const uint8_t *)passwd, strlen(passwd), priv);

	/* Generate public part. */
	if (crypto_dh_generate_pub(pub, priv))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
