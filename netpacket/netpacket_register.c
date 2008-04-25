#include "bsdtar_platform.h"

#include <stdlib.h>
#include <string.h>

#include "netpacket_internal.h"
#include "netproto.h"
#include "warnp.h"

#include "netpacket.h"

/**
 * netpacket_register_request(NPC, user, callback):
 * Construct and send a NETPACKET_REGISTER_REQUEST packet asking to register
 * a new machine belonging to the specified user.
 */
int
netpacket_register_request(NETPACKET_CONNECTION * NPC,
    const char * user, handlepacket_callback * callback)
{

	/* Make sure user name is a sane length. */
	if (strlen(user) > 255) {
		warn0("User name too long: %s", user);
		goto err0;
	}

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_REGISTER_REQUEST,
	    (const uint8_t *)user, strlen(user),
	    netpacket_op_packetsent, NPC))
		goto err0;

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * netpacket_register_cha_response(NPC, keys, name, register_key, callback):
 * Construct and send a NETPACKET_REGISTER_CHA_RESPONSE packet providing the
 * given access keys and user-friendly name, signed using the shared key
 * ${register_key} computed by hashing the Diffie-Hellman shared secret K.
 */
int netpacket_register_cha_response(NETPACKET_CONNECTION * NPC,
    const uint8_t keys[96], const char * name,
    const uint8_t register_key[32], handlepacket_callback * callback)
{
	size_t namelen;
	uint8_t * packetbuf;
	uint8_t prefixbuf[1];

	/* Allocate temporary space for constructing packet. */
	namelen = strlen(name);
	if ((packetbuf = malloc(129 + namelen)) == NULL)
		goto err0;

	/* Construct challenge response packet. */
	memcpy(packetbuf, keys, 96);
	packetbuf[96] = namelen;
	memcpy(packetbuf + 97, name, namelen);

	/* Append hmac. */
	prefixbuf[0] = NETPACKET_REGISTER_CHA_RESPONSE;
	crypto_hash_data_key_2(register_key, 32, prefixbuf, 1,
	    packetbuf, 97 + namelen, &packetbuf[97 + namelen]);

	/* Send challenge response packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_REGISTER_CHA_RESPONSE,
	    packetbuf, 129 + namelen, netpacket_op_packetsent, NPC))
		goto err1;

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Free temporary packet buffer. */
	free(packetbuf);

	/* Success! */
	return (0);

err1:
	free(packetbuf);
err0:
	/* Failure! */
	return (-1);
}
