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
 * netpacket_write_fexist(NPC, machinenum, class, name, nonce, callback):
 * Construct and send a NETPACKET_WRITE_FEXIST packet asking if the
 * specified file exists.
 */
int
netpacket_write_fexist(NETPACKET_CONNECTION * NPC,
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
	if (netpacket_hmac_append(NETPACKET_WRITE_FEXIST,
	    packetbuf, 73, CRYPTO_KEY_AUTH_PUT))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_WRITE_FEXIST,
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

/**
 * netpacket_write_file(NPC, machinenum, class, name, buf, buflen,
 *     nonce, callback):
 * Construct and send a NETPACKET_WRITE_FILE packet asking to write the
 * specified file.
 */
int
netpacket_write_file(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t class, const uint8_t name[32],
    const uint8_t * buf, size_t buflen, const uint8_t nonce[32],
    handlepacket_callback * callback)
{
	uint8_t * packetbuf;

	/* Sanity-check file size. */
	if (buflen > 262144) {
		warn0("file of class %c too large: (%zu > %zu)",
		    class, buflen, (size_t)262144);
		goto err0;
	}

	/* Allocate space for constructing packet. */
	if ((packetbuf = malloc(109 + buflen)) == NULL)
		goto err0;

	/* Construct packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = class;
	memcpy(&packetbuf[9], name, 32);
	memcpy(&packetbuf[41], nonce, 32);
	be32enc(&packetbuf[73], buflen);
	memcpy(&packetbuf[77], buf, buflen);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_WRITE_FILE, packetbuf,
	    77 + buflen, CRYPTO_KEY_AUTH_PUT))
		goto err1;

	/* Send packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_WRITE_FILE,
	    packetbuf, 109 + buflen, netpacket_op_packetsent, NPC))
		goto err1;

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Free packet construction buffer. */
	free(packetbuf);

	/* Success! */
	return (0);

err1:
	free(packetbuf);
err0:
	/* Failure! */
	return (-1);
}
