#include <stdint.h>

#include <openssl/err.h>
#include <openssl/rand.h>

#include "crypto_entropy.h"
#include "warnp.h"

#include "crypto.h"
#include "crypto_internal.h"

/* Amount of entropy to use for seeding OpenSSL. */
#define RANDBUFLEN	2048

/**
 * crypto_keys_init(void):
 * Initialize cryptographic keys.
 */
int
crypto_keys_init(void)
{
	uint8_t randbuf[RANDBUFLEN];

	/* Initialize key cache. */
	if (crypto_keys_init_keycache())
		goto err0;

	/* Load OpenSSL error strings. */
	ERR_load_crypto_strings();

	/* Seed OpenSSL entropy pool. */
	if (crypto_entropy_read(randbuf, RANDBUFLEN)) {
		warnp("Could not obtain sufficient entropy");
		goto err0;
	}
	RAND_seed(randbuf, RANDBUFLEN);

	/* Load server root public key. */
	if (crypto_keys_server_import_root()) {
		warn0("Could not import server root public key");
		goto err0;
	}

	/* Initialize keys owned by crypto_file. */
	if (crypto_file_init_keys()) {
		warn0("Could not initialize crypto_file keys");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
