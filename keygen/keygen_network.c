#include <string.h>

#include "keygen.h"
#include "netpacket.h"
#include "netproto.h"
#include "tsnetwork.h"
#include "sysendian.h"
#include "warnp.h"

static sendpacket_callback callback_register_send;
static handlepacket_callback callback_register_challenge;
static handlepacket_callback callback_register_response;

static int
callback_register_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct register_internal * C = cookie;

	/* Tell the server which user is trying to add a machine. */
	return (netpacket_register_request(NPC, C->user,
	    callback_register_challenge));
}

static int
callback_register_challenge(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct register_internal * C = cookie;
	uint8_t pub[CRYPTO_DH_PUBLEN];
	uint8_t priv[CRYPTO_DH_PRIVLEN];
	uint8_t K[CRYPTO_DH_KEYLEN];
	uint8_t keys[96];

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/*
	 * Make sure we received the right type of packet.  It is legal for
	 * the server to send back a NETPACKET_REGISTER_RESPONSE at this
	 * point; call callback_register_response to handle those.
	 */
	if (packettype == NETPACKET_REGISTER_RESPONSE)
		return (callback_register_response(cookie, NPC, status,
		    packettype, packetbuf, packetlen));
	else if (packettype != NETPACKET_REGISTER_CHALLENGE) {
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err0;
	}

	/* Generate DH parameters from the password and salt. */
	if (crypto_passwd_to_dh(C->passwd, packetbuf, pub, priv)) {
		warnp("Could not generate DH parameter from password");
		goto err0;
	}

	/* Compute shared key. */
	if (crypto_dh_compute(&packetbuf[32], priv, K))
		goto err0;
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, K, CRYPTO_DH_KEYLEN,
	    C->register_key)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Export access keys. */
	if (crypto_keys_raw_export_auth(keys))
		goto err0;

	/* Send challenge response packet. */
	if (netpacket_register_cha_response(NPC, keys, C->name,
	    C->register_key, callback_register_response))
		goto err0;

	/* We've responded to a challenge. */
	C->donechallenge = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_register_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct register_internal * C = cookie;
	uint8_t hmac_actual[32];

	(void)NPC; /* UNUSED */
	(void)packetlen; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_REGISTER_RESPONSE)
		goto err1;

	/* Verify packet hmac. */
	if ((packetbuf[0] == 0) || (packetbuf[0] == 3)) {
		crypto_hash_data_key_2(C->register_key, 32, &packettype, 1,
		    packetbuf, 9, hmac_actual);
	} else {
		memset(hmac_actual, 0, 32);
	}
	if (crypto_verify_bytes(hmac_actual, &packetbuf[9], 32))
		goto err1;

	/* Record status code and machine number returned by server. */
	C->status = packetbuf[0];
	C->machinenum = be64dec(&packetbuf[1]);

	/* We have received a response. */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}
