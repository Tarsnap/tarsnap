#include "bsdtar_platform.h"

#include <stdint.h>
#include <string.h>

#include "crypto.h"
#include "netpacket_internal.h"
#include "netproto.h"
#include "sysendian.h"

#include "netpacket.h"

/**
 * netpacket_delete_file(NPC, machinenum, class, name, nonce, callback):
 * Construct and send a NETPACKET_DELETE_FILE packet asking to delete the
 * specified file.
 */
int
netpacket_delete_file(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t class, const uint8_t name[32],
    const uint8_t nonce[32], handlepacket_callback * callback)
{
	uint8_t packetbuf[105];

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = class;
	memcpy(&packetbuf[9], name, 32);
	memcpy(&packetbuf[41], nonce, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_DELETE_FILE,
	    packetbuf, 73, CRYPTO_KEY_AUTH_DELETE))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_DELETE_FILE,
	    packetbuf, 105, netpacket_op_packetsent, NPC))
		goto err0;

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
