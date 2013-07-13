#include "bsdtar_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_dh.h"
#include "ctassert.h"
#include "warnp.h"

#include "crypto.h"
#include "netproto_internal.h"
#include "tsnetwork.h"

#include "netproto.h"

struct keyexchange_internal {
	char * useragent;
	uint8_t useragentlen;
	network_callback * callback;
	void * cookie;
	NETPROTO_CONNECTION * C;
	struct timeval timeout;
	size_t refcount;
	uint8_t serverproto;
	uint8_t serverparams[CRYPTO_DH_PUBLEN + 256 + 32];
	uint8_t pub[CRYPTO_DH_PUBLEN];
	uint8_t priv[CRYPTO_DH_PRIVLEN];
	uint8_t mkey[48];
	uint8_t clientproof[32];
	uint8_t serverproof[32];
};

static int docallback(struct keyexchange_internal *, int);
static network_callback proto_sent;
static network_callback proto_received;
static network_callback namelen_sent;
static network_callback name_sent;
static network_callback dh_received;
static network_callback dh_sent;
static network_callback proof_sent;
static network_callback proof_received;

/**
 * docallback(C, status):
 * Call ${C->callback} if it is non-NULL; and set it to NULL to make sure
 * that we don't call it again later.
 */
static int
docallback(struct keyexchange_internal * KC, int status)
{
	network_callback * cb = KC->callback;

	/* We don't want to call this again. */
	KC->callback = NULL;

	/* If we haven't called it before, call it now. */
	if (cb != NULL)
		return ((cb)(KC->cookie, status));
	else
		return (0);
}

/**
 * Connection negotiation and key exchange protocol:
 * Client                                Server
 * Protocol version (== 0; 1 byte)    ->
 *                                    <- Protocol version (== 0; 1 byte)
 * namelen (1 -- 255; 1 byte)         ->
 * User-agent name (namelen bytes)    ->
 *                                    <- 2^x mod p (CRYPTO_DH_PUBLEN bytes)
 *                                    <- RSA-PSS(2^x mod p) (256 bytes)
 *                                    <- nonce (random; 32 bytes)
 * 2^y mod p (CRYPTO_DH_PUBLEN bytes) ->
 * C_auth(mkey) (32 bytes)            ->
 *                                    <- S_auth(mkey) (32 bytes)
 *
 * Both sides compute K = 2^(xy) mod p.
 * Shared "master" key is mkey = MGF1(nonce || K, 48).
 * Server encryption key is S_encr = HMAC(mkey, "S_encr").
 * Server authentication key is S_auth = HMAC(mkey, "S_auth").
 * Client keys C_encr and C_auth are generated in the same way.
 *
 * This is cryptographically similar to SSL where the server has an
 * RSA_DH certificate, except that the client random is omitted (it is
 * unnecessary given that the client provides 256 bits of entropy via
 * its choice of 2^y mod p).
 */

static uint8_t protovers = 0;

/**
 * netproto_keyexchange(C, useragent, callback, cookie):
 * Perform protocol negotiation and key exchange with the tarsnap server
 * on the newly opened connection with cookie ${C}.  When the negotiation
 * is complete or has failed, call callback(cookie, status) where status is
 * a NETPROTO_STATUS_* value.
 */
int
netproto_keyexchange(NETPROTO_CONNECTION * C, const char * useragent,
    network_callback * callback, void * cookie)
{
	struct keyexchange_internal * KC;
	size_t useragentlen = strlen(useragent);

	/* Sanity-check user-agent string. */
	if ((useragentlen < 1) || (useragentlen > 255)) {
		warn0("Programmer error: "
		    "User-agent string has invalid length (%zu): %s",
		    useragentlen, useragent);
		goto err0;
	}

	/* Create keyexchange cookie. */
	if ((KC = malloc(sizeof(struct keyexchange_internal))) == NULL)
		goto err0;
	if ((KC->useragent = strdup(useragent)) == NULL)
		goto err1;
	KC->useragentlen = useragentlen;
	KC->callback = callback;
	KC->cookie = cookie;
	KC->C = C;
	KC->timeout.tv_sec = 5;
	KC->timeout.tv_usec = 0;
	KC->refcount = 1;

	/* Send protocol version. */
	if (network_writeq_add(KC->C->Q, &protovers, 1, &KC->timeout,
	    proto_sent, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	free(KC->useragent);
err1:
	free(KC);
err0:
	/* Failure! */
	return (-1);
}

static int
proto_sent(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesout += 1;

	/* Data was sent.  Read the server protocol version. */
	if (tsnetwork_read(KC->C->fd, &KC->serverproto, 1, &KC->timeout,
	    &KC->timeout, proto_received, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
proto_received(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesin += 1;

	/* Make sure the protocol version is zero. */
	if (KC->serverproto != 0) {
		status = NETPROTO_STATUS_PROTERR;
		goto err1;
	}

	/* Send our identity. */
	if (network_writeq_add(KC->C->Q, &KC->useragentlen, 1, &KC->timeout,
	    namelen_sent, KC))
		goto err2;
	KC->refcount++;
	if (network_writeq_add(KC->C->Q, (const uint8_t *)KC->useragent,
	    KC->useragentlen, &KC->timeout, name_sent, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
namelen_sent(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesout += 1;

	/* We're not asking for another callback. */
	KC->refcount--;

	/* Success! */
	return (0);

err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
name_sent(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesout += KC->useragentlen;

	/* Data was sent.  Read the server crypto parameters. */
	if (tsnetwork_read(KC->C->fd, KC->serverparams,
	    CRYPTO_DH_PUBLEN + 256 + 32, &KC->timeout, &KC->timeout,
	    dh_received, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
dh_received(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesin += CRYPTO_DH_PUBLEN + 256 + 32;

	/* Verify the signature on the DH public value. */
	switch (crypto_rsa_verify(CRYPTO_KEY_ROOT_PUB, KC->serverparams,
	    CRYPTO_DH_PUBLEN, KC->serverparams + CRYPTO_DH_PUBLEN, 256)) {
	case -1:
		/* Internal error in crypto_rsa_verify. */
		goto err2;
	case 1:
		/* Bad signature. */
		status = NETPROTO_STATUS_PROTERR;
		goto err1;
	case 0:
		/* Good signature. */
		break;
	}

	/* Sanity-check the received public Diffie-Hellman value. */
	if (crypto_dh_sanitycheck(KC->serverparams)) {
		/* Value is insane. */
		status = NETPROTO_STATUS_PROTERR;
		goto err1;
	}

	/* Generate DH pair and send public value to server. */
	if (crypto_dh_generate(KC->pub, KC->priv))
		goto err2;
	if (network_writeq_add(KC->C->Q, KC->pub, CRYPTO_DH_PUBLEN,
	    &KC->timeout, dh_sent, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
dh_sent(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesout += CRYPTO_DH_PUBLEN;

	/*-
	 * NOTE: We could construct the session keys, and compute and send
	 * the proof of key possession in dh_received instead of here; doing
	 * so would reduce the bandwidth used since the two messages of 256
	 * and 32 bytes would be coalesced into a single TCP/IP packet, but
	 * it would slow down the connection setup since it would prevent the
	 * client and server computations of the session keys from being
	 * overlapped.
	 *
	 * The trade-off here is between ~40 bytes of bandwidth and ~15ms of
	 * latency; at typical internet bandwidth costs of < $1/GB, taking
	 * the faster but bandwidth-wasting option is preferable if latency
	 * costs more than $0.01/hour, while at (extortionate) mobile phone
	 * data rates of $5/MB the bandwidth-saving but slower option is
	 * better as long as latency costs less than $45/hour.
	 *
	 * Since I don't expect people to be running tarsnap from mobile
	 * phones (or over 19.2Kbps modems, where the added bandwidth slows
	 * down the connection by longer than the overlapping of computation
	 * time saves) any time soon, I'm taking the route which optimizes
	 * for time.
	 */

	/* Construct session keys. */
	if ((KC->C->keys = crypto_session_init(KC->serverparams, KC->priv,
	    KC->serverparams + CRYPTO_DH_PUBLEN + 256, KC->mkey,
	    "C_encr", "C_auth", "S_encr", "S_auth")) == NULL)
		goto err2;

	/* Construct proof of key possession and send to server. */
	crypto_session_sign(KC->C->keys, KC->mkey, 48, KC->clientproof);
	if (network_writeq_add(KC->C->Q, KC->clientproof, 32, &KC->timeout,
	    proof_sent, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
proof_sent(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	if (status != NETWORK_STATUS_OK)
		goto err1;

	/* Adjust traffic statistics. */
	KC->C->bytesout += 32;

	/* Client proof was sent.  Read the server proof. */
	if (tsnetwork_read(KC->C->fd, KC->serverproof, 32, &KC->timeout,
	    &KC->timeout, proof_received, KC))
		goto err2;

	/* Success! */
	return (0);

err2:
	status = NETWORK_STATUS_ERR;
err1:
	/* Something went wrong.  Let the callback handle it. */
	rc = docallback(KC, status);
	if (KC->refcount-- == 1) {
		free(KC->useragent);
		free(KC);
	}

	/* Failure! */
	return (rc);
}

static int
proof_received(void * cookie, int status)
{
	struct keyexchange_internal * KC = cookie;
	int rc;

	/* This should be our last reference. */
	if (KC->refcount != 1) {
		warn0("Wrong # of references: %zu", KC->refcount);
		goto err0;
	}

	/* Might we have been successful? */
	if (status == NETWORK_STATUS_OK) {
		/* Adjust traffic statistics. */
		KC->C->bytesin += 32;

		/* Verify that the server proof is valid. */
		if (crypto_session_verify(KC->C->keys, KC->mkey, 48,
		    KC->serverproof))
			status = NETPROTO_STATUS_PROTERR;
	}

	/* We're done or errored out -- either way, call the callback. */
	rc = docallback(KC, status);

	/* Free the cookie. */
	free(KC->useragent);
	free(KC);

	/* Return the value from the callback. */
	return (rc);

err0:
	/* Failure! */
	return (-1);
}
