#include "bsdtar_platform.h"

#include <stdint.h>
#include <string.h>

#include "crypto.h"
#include "netpacket_internal.h"
#include "netproto.h"
#include "sysendian.h"

#include "netpacket.h"

/**
 * netpacket_directory(NPC, machinenum, class, start, snonce, cnonce, key,
 *     callback):
 * Construct and send a NETPACKET_DIRECTORY packet (if key == 0) or
 * NETPACKET_DIRECTORY_D packet (otherwise) asking for a list of files
 * of the specified class starting from the specified position.
 */
int
netpacket_directory(NETPACKET_CONNECTION * NPC, uint64_t machinenum,
    uint8_t class, const uint8_t start[32], const uint8_t snonce[32],
    const uint8_t cnonce[32], int key, handlepacket_callback * callback)
{
	uint8_t packetbuf[137];

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = class;
	memcpy(&packetbuf[9], start, 32);
	memcpy(&packetbuf[41], snonce, 32);
	memcpy(&packetbuf[73], cnonce, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(
	    (key == 0) ? NETPACKET_DIRECTORY : NETPACKET_DIRECTORY_D,
	    packetbuf, 105,
	    (key == 0) ? CRYPTO_KEY_AUTH_GET : CRYPTO_KEY_AUTH_DELETE))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC,
	    (key == 0) ? NETPACKET_DIRECTORY : NETPACKET_DIRECTORY_D,
	    packetbuf, 137, netpacket_op_packetsent, NPC))
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
 * netpacket_directory_readmore(NPC, callback):
 * Read more NETPACKET_DIRECTORY_RESPONSE packets.
 */
int
netpacket_directory_readmore(NETPACKET_CONNECTION * NPC,
    handlepacket_callback * callback)
{

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Success! */
	return (0);
}
