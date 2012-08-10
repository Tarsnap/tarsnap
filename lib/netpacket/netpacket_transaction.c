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
 * netpacket_transaction_getnonce(NPC, machinenum, callback):
 * Construct and send a NETPACKET_TRANSACTION_GETNONCE packet asking to get
 * a transaction server nonce.
 */
int
netpacket_transaction_getnonce(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, handlepacket_callback * callback)
{
	uint8_t packetbuf[8];

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_TRANSACTION_GETNONCE,
	    packetbuf, 8, netpacket_op_packetsent, NPC))
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
 * netpacket_transaction_start(NPC, machinenum, operation, snonce, cnonce,
 *     state, callback):
 * Construct and send a NETPACKET_TRANSACTION_START packet asking to
 * start a transaction; the transaction is a write transaction if
 * ${operation} is 0, a delete transaction if ${operation} is 1, or a fsck
 * transaction if ${operation} is 2.
 */
int
netpacket_transaction_start(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t operation, const uint8_t snonce[32],
    const uint8_t cnonce[32], const uint8_t state[32],
    handlepacket_callback * callback)
{
	uint8_t packetbuf[137];
	int key;

	/* Look up the key which is used to sign this packet. */
	switch (operation) {
	case 0:	/* Write. */
	case 3:	/* Read-only fsck using the write key. */
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:	/* Delete. */
	case 2:	/* Fsck. */
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: "
		    "Invalid operation in netpacket_transaction_start");
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = operation;
	memcpy(&packetbuf[9], snonce, 32);
	memcpy(&packetbuf[41], cnonce, 32);
	memcpy(&packetbuf[73], state, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_TRANSACTION_START,
	    packetbuf, 105, key))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_TRANSACTION_START,
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
 * netpacket_transaction_commit(NPC, machinenum, whichkey, nonce, callback):
 * Construct and send a NETPACKET_TRANSACTION_COMMIT packet asking to commit
 * a transaction; the packet is signed with the write access key if
 * ${whichkey} is 0, and with the delete access key if ${whichkey} is 1.
 */
int
netpacket_transaction_commit(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t whichkey, const uint8_t nonce[32],
    handlepacket_callback * callback)
{
	uint8_t packetbuf[73];
	int key;

	/* Look up the key which is used to sign this packet. */
	switch (whichkey) {
	case 0:
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: "
		    "Invalid key in netpacket_transaction_commit");
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = whichkey;
	memcpy(&packetbuf[9], nonce, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_TRANSACTION_COMMIT,
	    packetbuf, 41, key))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_TRANSACTION_COMMIT,
	    packetbuf, 73, netpacket_op_packetsent, NPC))
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
 * netpacket_transaction_checkpoint(NPC, machinenum, whichkey, ckptnonce,
 *     nonce, callback):
 * Construct and send a NETPACKET_TRANSACTION_CHECKPOINT packet asking to
 * create a checkpoint in a write transaction.
 */
int
netpacket_transaction_checkpoint(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t whichkey, const uint8_t ckptnonce[32],
    const uint8_t nonce[32], handlepacket_callback * callback)
{
	uint8_t packetbuf[105];
	int key;

	/* Look up the key which is used to sign this packet. */
	switch (whichkey) {
	case 0:
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: "
		    "Invalid key in netpacket_transaction_commit");
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = whichkey;
	memcpy(&packetbuf[9], ckptnonce, 32);
	memcpy(&packetbuf[41], nonce, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_TRANSACTION_CHECKPOINT,
	    packetbuf, 73, key))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_TRANSACTION_CHECKPOINT,
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
 * netpacket_transaction_cancel(NPC, machinenum, whichkey, snonce, cnonce,
 *     state, callback):
 * Construct and send a NETPACKET_TRANSACTION_CANCEL packet asking to cancel
 * a pending transaction if the state is correct.
 */
int
netpacket_transaction_cancel(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t whichkey, const uint8_t snonce[32],
    const uint8_t cnonce[32], const uint8_t state[32],
    handlepacket_callback * callback)
{
	uint8_t packetbuf[137];
	int key;

	/* Look up the key which is used to sign this packet. */
	switch (whichkey) {
	case 0:	/* Write key. */
	case 3:	/* Write key and state = 0. */
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:	/* Delete key. */
	case 2:	/* Delete key and state = 0. */
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: "
		    "Invalid operation in netpacket_transaction_cancel");
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = whichkey;
	memcpy(&packetbuf[9], snonce, 32);
	memcpy(&packetbuf[41], cnonce, 32);
	memcpy(&packetbuf[73], state, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_TRANSACTION_CANCEL,
	    packetbuf, 105, key))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_TRANSACTION_CANCEL,
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
 * netpacket_transaction_trycommit(NPC, machinenum, whichkey, nonce,
 *     callback):
 * Construct and send a NETPACKET_TRANSACTION_TRYCOMMIT packet asking to
 * commit a transaction; the packet is signed with the write access key if
 * ${whichkey} is 0, and with the delete access key if ${whichkey} is 1.
 */
int
netpacket_transaction_trycommit(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t whichkey, const uint8_t nonce[32],
    handlepacket_callback * callback)
{
	uint8_t packetbuf[73];
	int key;

	/* Look up the key which is used to sign this packet. */
	switch (whichkey) {
	case 0:
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: "
		    "Invalid key in netpacket_transaction_trycommit");
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = whichkey;
	memcpy(&packetbuf[9], nonce, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_TRANSACTION_TRYCOMMIT,
	    packetbuf, 41, key))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC, NETPACKET_TRANSACTION_TRYCOMMIT,
	    packetbuf, 73, netpacket_op_packetsent, NPC))
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
 * netpacket_transaction_ischeckpointed(NPC, machinenum, whichkey, nonce,
 *     callback):
 * Construct and send a NETPACKET_TRANSACTION_ISCHECKPOINTED packet asking if
 * a checkpointed write transaction is in progress; the packet is signed with
 * the write access key if ${whichkey} is 0, and with the delete access key
 * if ${whichkey} is 1.
 */
int
netpacket_transaction_ischeckpointed(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t whichkey, const uint8_t nonce[32],
    handlepacket_callback * callback)
{
	uint8_t packetbuf[73];
	int key;

	/* Look up the key which is used to sign this packet. */
	switch (whichkey) {
	case 0:
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: "
		    "Invalid key in netpacket_transaction_ischeckpointed");
		goto err0;
	}

	/* Construct the packet. */
	be64enc(&packetbuf[0], machinenum);
	packetbuf[8] = whichkey;
	memcpy(&packetbuf[9], nonce, 32);

	/* Append hmac. */
	if (netpacket_hmac_append(NETPACKET_TRANSACTION_ISCHECKPOINTED,
	    packetbuf, 41, key))
		goto err0;

	/* Send the packet. */
	if (netproto_writepacket(NPC->NC,
	    NETPACKET_TRANSACTION_ISCHECKPOINTED,
	    packetbuf, 73, netpacket_op_packetsent, NPC))
		goto err0;

	/* Set callback for handling a response. */
	NPC->pending_current->handlepacket = callback;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
