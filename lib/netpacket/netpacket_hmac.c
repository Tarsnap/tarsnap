#include "bsdtar_platform.h"

#include <string.h>

#include "crypto_verify_bytes.h"

#include "crypto.h"

#include "netpacket.h"
#include "netpacket_internal.h"

/**
 * netpacket_hmac_append(type, packetbuf, len, key):
 * HMAC (type || packetbuf[0 .. len - 1]) using the specified key and write
 * the result into packetbuf[len .. len + 31].
 */
int
netpacket_hmac_append(uint8_t type, uint8_t * packetbuf, size_t len, int key)
{

	return (crypto_hash_data_2(key, &type, 1, packetbuf, len,
	    &packetbuf[len]));
}

/**
 * netpacket_hmac_verify(type, nonce, packetbuf, pos, key):
 * Verify that HMAC(type || nonce || packetbuf[0 .. pos - 1]) using the
 * specified key matches packetbuf[pos .. pos + 31].  If nonce is NULL, omit
 * it from the data being HMACed as appropriate.  Return -1 on error, 0 on
 * success, or 1 if the hash does not match.
 */
int
netpacket_hmac_verify(uint8_t type, const uint8_t nonce[32],
    const uint8_t * packetbuf, size_t pos, int key)
{
	uint8_t hmac_actual[32];
	uint8_t prefixbuf[33];
	size_t prefixlen;

	/* Compute the correct HMAC. */
	prefixbuf[0] = type;
	prefixlen = 1;
	if (nonce != NULL) {
		memcpy(&prefixbuf[prefixlen], nonce, 32);
		prefixlen += 32;
	}
	if (crypto_hash_data_2(key, prefixbuf, prefixlen,
	    packetbuf, pos, hmac_actual))
		goto err0;

	/* Compare. */
	if (crypto_verify_bytes(&packetbuf[pos], hmac_actual, 32))
		goto badhmac;

	/* Success! */
	return (0);

badhmac:
	/* HMAC doesn't match. */
	return (1);

err0:
	/* Failure! */
	return (-1);
}
