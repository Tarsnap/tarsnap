#include "bsdtar_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "netpacket.h"
#include "netproto.h"
#include "storage_internal.h"
#include "warnp.h"

#include "storage.h"

/*
 * Maximum number of delete operations which are allowed to be pending
 * before storage_delete_file will block.
 */
#define MAXPENDING_DELETE	1024

struct storage_delete_internal {
	/* Transaction parameters. */
	NETPACKET_CONNECTION * NPC;
	uint64_t machinenum;
	uint8_t nonce[32];

	/* Are we not allowed to delete files? */
	int readonly;

	/* Number of pending deletes. */
	size_t npending;
};

struct delete_file_internal {
	/* Pointer to transaction to which this belongs. */
	struct storage_delete_internal * S;

	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in delete_file. */
	uint8_t class;
	uint8_t name[32];
	uint8_t nonce[32];
};

static sendpacket_callback callback_delete_file_send;
static handlepacket_callback callback_delete_file_response;

/**
 * storage_delete_start(machinenum, lastseq, seqnum):
 * Start a delete transaction, presuming that ${lastseq} is the sequence
 * number of the last committed transaction, or zeroes if there is no
 * previous transaction; and store the sequence number of the new transaction
 * into ${seqnum}.
 */
STORAGE_D *
storage_delete_start(uint64_t machinenum, const uint8_t lastseq[32],
    uint8_t seqnum[32])
{
	struct storage_delete_internal * S;

	/* Allocate memory. */
	if ((S = malloc(sizeof(struct storage_delete_internal))) == NULL)
		goto err0;

	/* Store machine number. */
	S->machinenum = machinenum;

	/* No pending deletes so far. */
	S->npending = 0;

	/* This is not readonly. */
	S->readonly = 0;

	/* Open netpacket connection. */
	if ((S->NPC = netpacket_open(USERAGENT)) == NULL)
		goto err1;

	/* Start a delete transaction. */
	if (storage_transaction_start_delete(S->NPC, machinenum, lastseq,
	    S->nonce))
		goto err2;

	/* Copy the transaction nonce out. */
	memcpy(seqnum, S->nonce, 32);

	/* Success! */
	return (S);

err2:
	netpacket_close(S->NPC);
err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * storage_fsck_start(machinenum, seqnum, readonly, whichkey):
 * Start a fsck transaction, and store the sequence number of said
 * transaction into ${seqnum}.  If ${whichkey} is zero, use the write key
 * (in which case the transaction must be readonly).
 */
STORAGE_D *
storage_fsck_start(uint64_t machinenum, uint8_t seqnum[32],
    int readonly, int whichkey)
{
	struct storage_delete_internal * S;

	/* Allocate memory. */
	if ((S = malloc(sizeof(struct storage_delete_internal))) == NULL)
		goto err0;

	/* Store machine number. */
	S->machinenum = machinenum;

	/* No pending deletes so far. */
	S->npending = 0;

	/* This is not readonly. */
	S->readonly = readonly;

	/* Open netpacket connection. */
	if ((S->NPC = netpacket_open(USERAGENT)) == NULL)
		goto err1;

	/* Start a delete transaction. */
	if (storage_transaction_start_fsck(S->NPC, machinenum, S->nonce,
	    whichkey))
		goto err2;

	/* Copy the transaction nonce out. */
	memcpy(seqnum, S->nonce, 32);

	/* Success! */
	return (S);

err2:
	netpacket_close(S->NPC);
err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * storage_delete_file(S, class, name):
 * Delete the file ${name} from class ${class} as part of the delete
 * transaction associated with the cookie ${S}.
 */
int
storage_delete_file(STORAGE_D * S, char class, const uint8_t name[32])
{
	struct delete_file_internal * C;

	if (S->readonly) {
		warn0("Not pruning corrupted data; please run --fsck-prune");
		goto err0;
	}

	/* Create delete cookie. */
	if ((C = malloc(sizeof(struct delete_file_internal))) == NULL)
		goto err0;
	C->S = S;
	C->machinenum = S->machinenum;
	C->class = class;
	memcpy(C->name, name, 32);
	memcpy(C->nonce, S->nonce, 32);
	C->done = 0;

	/* We're issuing a delete operation. */
	S->npending += 1;

	/*
	 * Make sure the pending operation queue isn't too large before we
	 * add yet another operation to it.
	 */
	if (S->npending > MAXPENDING_DELETE) {
		/* Avoid silly window syndrome. */
		while (S->npending > MAXPENDING_DELETE / 2 + 1) {
			if (network_select(1))
				goto err1;
		}
	}

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(S->NPC, callback_delete_file_send, C))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_delete_file_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct delete_file_internal * C = cookie;

	/* Ask the server to delete the file in question. */
	return (netpacket_delete_file(NPC, C->machinenum, C->class,
	    C->name, C->nonce, callback_delete_file_response));
}

static int
callback_delete_file_response(void * cookie,
    NETPACKET_CONNECTION * NPC, int status, uint8_t packettype,
    const uint8_t * packetbuf, size_t packetlen)
{
	struct delete_file_internal * C = cookie;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err1;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_DELETE_FILE_RESPONSE)
		goto err2;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->nonce,
	    packetbuf, 34, CRYPTO_KEY_AUTH_DELETE)) {
	case 1:
		goto err2;
	case -1:
		goto err1;
	}

	/* Make sure that the packet corresponds to the right file. */
	if ((packetbuf[1] != C->class) ||
	    (memcmp(&packetbuf[2], C->name, 32)))
		goto err2;

	/* Parse status returned by server. */
	switch (packetbuf[0]) {
	case 0:
		/* This delete operation is no longer pending. */
		C->S->npending -= 1;
		break;
	case 1:
		warn0("Cannot delete file: File does not exist");
		goto err1;
	case 2:
		/* Bad nonce. */
		warn0("Delete transaction interrupted");
		goto err1;
	default:
		goto err2;
	}

	/* Free delete cookie. */
	free(C);

	/* Success! */
	return (0);

err2:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err1:
	free(C);

	/* Failure! */
	return (-1);
}

/**
 * storage_delete_flush(S):
 * Make sure all operations performed as part of the transaction associated
 * with the cookie ${S} have been safely stored in preparation for being
 * committed.
 */
int
storage_delete_flush(STORAGE_D * S)
{

	/* Wait until all pending deletes have been completed. */
	while (S->npending > 0) {
		if (network_select(1))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_delete_end(S):
 * Make sure that all operations performed as part of the transaction
 * associated with the cookie ${S} have been safely stored in
 * preparation for being committed; and close the transaction and free
 * associated memory.
 */
int
storage_delete_end(STORAGE_D * S)
{

	/* Flush any pending deletes. */
	if (storage_delete_flush(S))
		goto err2;

	/* Close netpacket connection. */
	if (netpacket_close(S->NPC))
		goto err1;

	/* Free structure. */
	free(S);

	/* Success! */
	return (0);

err2:
	netpacket_close(S->NPC);
err1:
	free(S);

	/* Failure! */
	return (-1);
}

/**
 * storage_delete_free(S):
 * Free any memory allocated as part of the delete transaction associated
 * with the cookie ${S}; the transaction will not be committed.
 */
void
storage_delete_free(STORAGE_D * S)
{

	/* Close netpacket connection. */
	netpacket_close(S->NPC);

	/* Free structure. */
	free(S);
}
