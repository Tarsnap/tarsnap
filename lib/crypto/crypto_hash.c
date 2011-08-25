#include "bsdtar_platform.h"

#include <stdint.h>
#include <string.h>

#include "crypto_internal.h"
#include "sha256.h"
#include "warnp.h"

#include "crypto.h"

/**
 * crypto_hash_data_key(key, keylen, data, len, buf):
 * Hash the provided data with the provided HMAC-SHA256 key.
 */
void
crypto_hash_data_key(const uint8_t * key, size_t keylen,
    const uint8_t * data, size_t len, uint8_t buf[32])
{

	/* Use crypto_hash_data_key_2 to do the work. */
	crypto_hash_data_key_2(key, keylen, data, len, NULL, 0, buf);
}

/**
 * crypto_hash_data_key_2(key, keylen, data0, len0, data1, len1, buf):
 * Hash the concatenation of two buffers with the provided HMAC-SHA256 key.
 */
void
crypto_hash_data_key_2(const uint8_t * key, size_t keylen,
    const uint8_t * data0, size_t len0,
    const uint8_t * data1, size_t len1, uint8_t buf[32])
{
	HMAC_SHA256_CTX hctx;

	/* Do the hashing. */
	HMAC_SHA256_Init(&hctx, key, keylen);
	HMAC_SHA256_Update(&hctx, data0, len0);
	HMAC_SHA256_Update(&hctx, data1, len1);
	HMAC_SHA256_Final(buf, &hctx);

	/* Clean the stack. */
	memset(&hctx, 0, sizeof(HMAC_SHA256_CTX));
}

/**
 * crypto_hash_data(key, data, len, buf):
 * Hash the provided data with the HMAC-SHA256 key specified; or if
 * ${key} == CRYPTO_KEY_HMAC_SHA256, just SHA256 the data.
 */
int
crypto_hash_data(int key, const uint8_t * data, size_t len, uint8_t buf[32])
{

	/* Use crypto_hash_data_2 to do the work. */
	return (crypto_hash_data_2(key, data, len, NULL, 0, buf));
}

/**
 * crypto_hash_data_2(key, data0, len0, data1, len1, buf):
 * Hash the concatenation of two buffers, as in crypto_hash_data.
 */
int
crypto_hash_data_2(int key, const uint8_t * data0, size_t len0,
    const uint8_t * data1, size_t len1, uint8_t buf[32])
{
	HMAC_SHA256_CTX hctx;
	SHA256_CTX ctx;
	struct crypto_hmac_key * hkey;

	if (key == CRYPTO_KEY_HMAC_SHA256) {
		/* Hash the data. */
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, data0, len0);
		SHA256_Update(&ctx, data1, len1);
		SHA256_Final(buf, &ctx);

		/* Clean the stack. */
		memset(&ctx, 0, sizeof(SHA256_CTX));
	} else {
		if ((hkey = crypto_keys_lookup_HMAC(key)) == NULL)
			goto err0;

		/* Do the HMAC. */
		HMAC_SHA256_Init(&hctx, hkey->key, hkey->len);
		HMAC_SHA256_Update(&hctx, data0, len0);
		HMAC_SHA256_Update(&hctx, data1, len1);
		HMAC_SHA256_Final(buf, &hctx);

		/* Clean the stack. */
		memset(&hctx, 0, sizeof(HMAC_SHA256_CTX));
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
