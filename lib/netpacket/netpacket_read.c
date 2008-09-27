#include "bsdtar_platform.h"

#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "netpacket_internal.h"
#include "netproto.h"
#include "sysendian.h"
#include "warnp.h"

#include "netpacket.h"

/**
 * netpacket_read_file(NPC, machinenum, class, name, size, callback):
 * Construct and send a NETPACKET_READ_FILE packet over the network
 * protocol connection ${NC} asking to read the specified file, which should
 * be ${size} (<= 262144) bytes long if ${size} is not (uint32_t)(-1).
 */
int
netpacket_read_file(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t class, const uint8_t name[32],
    uint32_t size, handlepacket_callback * callback)
{
	uint8_t packetbuf[77];

	/* Sanity-check size. */
	if ((size > 262144) && (size != (uint32_t)(-1))) {
		warn0("file of class %c too large: (%zu > %zu)",
		    class, (size_t)size, (size_t)262144);
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = class;
	memcpy(&packetbuf[9], name, 32);
	be32enc(&packetbuf[41], size);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_READ_FILE,
	    packetbuf, 45, CRYPTO_KEY_AUTH_GET))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_READ_FILE,
	    packetbuf, 77, netpacket_op_packetsent, NPC))
		goto err0;

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
