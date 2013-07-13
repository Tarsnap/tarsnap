#include "bsdtar_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "crypto.h"
#include "crypto_entropy.h"
#include "netpacket.h"
#include "netproto.h"
#include "warnp.h"

#include "storage.h"
#include "storage_internal.h"

struct transaction_cancel_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in cancelling a transaction. */
	uint8_t lastseq[32];
	uint8_t whichkey;
	uint8_t snonce[32];
	uint8_t cnonce[32];
	uint8_t seqnum[32];
	int status;
};

struct transaction_start_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in starting a transaction. */
	uint8_t lastseq[32];
	uint8_t type;
	uint8_t snonce[32];
	uint8_t cnonce[32];
	uint8_t seqnum[32];
	int status;
};

struct transaction_checkpoint_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in creating a checkpoint. */
	uint8_t seqnum[32];
	uint8_t ckptnonce[32];
	uint8_t whichkey;
	int status;
};

struct transaction_commit_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in committing a transaction. */
	uint8_t seqnum[32];
	uint8_t whichkey;
	int status;
};

struct transaction_ischeckpointed_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in the operation. */
	uint8_t whichkey;
	uint8_t nonce[32];
	int status;
	uint8_t tnonce[32];
};

static int key_lookup(int);
static int storage_transaction_cancel(NETPACKET_CONNECTION *, uint64_t,
    const uint8_t[32], uint8_t);
static int storage_transaction_start(NETPACKET_CONNECTION *, uint64_t,
    const uint8_t[32], uint8_t[32], uint8_t);
static sendpacket_callback callback_getnonce_cancel_send;
static handlepacket_callback callback_getnonce_cancel_response;
static handlepacket_callback callback_cancel_response;
static sendpacket_callback callback_getnonce_send;
static handlepacket_callback callback_getnonce_response;
static handlepacket_callback callback_start_response;
static sendpacket_callback callback_checkpoint_send;
static handlepacket_callback callback_checkpoint_response;
static sendpacket_callback callback_commit_send;
static handlepacket_callback callback_commit_response;
static sendpacket_callback callback_ischeckpointed_send;
static handlepacket_callback callback_ischeckpointed_response;

/**
 * key_lookup(type):
 * Look up the key number for the operation ${type}.
 */
static int
key_lookup(int type)
{
	int key;

	switch (type) {
	case 0:
	case 3:
		key = CRYPTO_KEY_AUTH_PUT;
		break;
	case 1:
	case 2:
		key = CRYPTO_KEY_AUTH_DELETE;
		break;
	default:
		warn0("Programmer error: Invalid transaction type");
		key = -1;
		break;
	}

	return (key);
}

/**
 * storage_transaction_cancel(NPC, machinenum, lastseq, whichkey):
 * Cancel any existing transaction, using the key specified by ${whichkey}.
 * If ${lastseq} is not the sequence number of the last committed transaction
 * and ${whichkey} does not indicate fscking, then this is a no-op.
 */
static int
storage_transaction_cancel(NETPACKET_CONNECTION * NPC, uint64_t machinenum,
    const uint8_t lastseq[32], uint8_t whichkey)
{
	struct transaction_cancel_internal C;

	/* Initialize transaction structure. */
	C.machinenum = machinenum;
	if ((whichkey != 2) && (whichkey != 3))
		memcpy(C.lastseq, lastseq, 32);
	else
		memset(C.lastseq, 0, 32);
	C.whichkey = whichkey;

	/*
	 * Ask the server to cancel any in-progress transaction; if it asks
	 * us to go away and come back later, sleep 1 second and then poke it
	 * again.
	 */
	do {
		/* Send a cancel request and get a response. */
		C.done = 0;
		if (netpacket_op(NPC, callback_getnonce_cancel_send, &C))
			goto err0;

		/* Wait for the response to come back. */
		if (network_spin(&C.done))
			goto err0;

		/* If the response was "success", stop looping. */
		if (C.status == 0)
			break;

		/* Sanity check status. */
		if (C.status != 1) {
			netproto_printerr(NETPROTO_STATUS_PROTERR);
			goto err0;
		}

		/* Give the server a chance to perform the cancel. */
		sleep(1);
	} while (1);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_getnonce_cancel_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct transaction_cancel_internal * C = cookie;

	/* Ask the server to provide a transaction server nonce. */
	return (netpacket_transaction_getnonce(NPC, C->machinenum,
	    callback_getnonce_cancel_response));
}

static int
callback_getnonce_cancel_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_cancel_internal * C = cookie;

	(void)packetlen; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_GETNONCE_RESPONSE)
		goto err1;

	/* Store the provided server nonce. */
	memcpy(C->snonce, packetbuf, 32);

	/* Generate a random client nonce. */
	if (crypto_entropy_read(C->cnonce, 32))
		goto err0;

	/* Send a transaction cancel request. */
	if (netpacket_transaction_cancel(NPC, C->machinenum,
	    C->whichkey, C->snonce, C->cnonce, C->lastseq,
	    callback_cancel_response))
		goto err0;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_cancel_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_cancel_internal * C = cookie;
	int key;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_CANCEL_RESPONSE)
		goto err1;

	/* Compute nonce used for signing response packet. */
	if (crypto_hash_data_2(CRYPTO_KEY_HMAC_SHA256, C->snonce, 32,
	    C->cnonce, 32, C->seqnum)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Look up packet signing key. */
	if ((key = key_lookup(C->whichkey)) == -1)
		goto err0;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->seqnum,
	    packetbuf, 1, key)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Store response code from server. */
	C->status = packetbuf[0];

	/* We're done! */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_transaction_start(NPC, machinenum, lastseq, seqnum, type):
 * Start a transaction of type ${type}.
 */
static int
storage_transaction_start(NETPACKET_CONNECTION * NPC, uint64_t machinenum,
    const uint8_t lastseq[32], uint8_t seqnum[32], uint8_t type)
{
	struct transaction_start_internal C;

	/* First cancel any existing transaction. */
	if (storage_transaction_cancel(NPC, machinenum, lastseq, type))
		goto err0;

	/* Initialize transaction structure. */
	C.machinenum = machinenum;
	if ((type != 2) && (type != 3))
		memcpy(C.lastseq, lastseq, 32);
	else
		memset(C.lastseq, 0, 32);
	C.type = type;

	/* Ask the netpacket layer to send a request and get a response. */
	C.done = 0;
	if (netpacket_op(NPC, callback_getnonce_send, &C))
		goto err0;

	/* Wait until the transaction has been started or failed. */
	if (network_spin(&C.done))
		goto err0;

	/* Report sequence number mismatch if necessary. */
	switch (C.status) {
	case 0:
		/* Success. */
		break;
	case 1:
		warn0("Sequence number mismatch: Run --fsck");
		goto err0;
	case 2:
		/* We should only get this for a write transaction start. */
		if (type != 0) {
			netproto_printerr(NETPROTO_STATUS_PROTERR);
			goto err0;
		}

		warn0("Cannot start write transaction: "
		    "Account balance is not positive.");
		warn0("Please add more money to your tarsnap account");
		goto err0;
	default:
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err0;
	}

	/* Store sequence number of new transaction. */
	memcpy(seqnum, C.seqnum, 32);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_getnonce_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct transaction_start_internal * C = cookie;

	/* Ask the server to provide a transaction server nonce. */
	return (netpacket_transaction_getnonce(NPC, C->machinenum,
	    callback_getnonce_response));
}

static int
callback_getnonce_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_start_internal * C = cookie;

	(void)packetlen; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_GETNONCE_RESPONSE)
		goto err1;

	/* Store the provided server nonce. */
	memcpy(C->snonce, packetbuf, 32);

	/* Generate a random client nonce. */
	if (crypto_entropy_read(C->cnonce, 32))
		goto err0;

	/* Send a transaction start request. */
	if (netpacket_transaction_start(NPC, C->machinenum,
	    C->type, C->snonce, C->cnonce, C->lastseq,
	    callback_start_response))
		goto err0;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_start_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_start_internal * C = cookie;
	int key;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_START_RESPONSE)
		goto err1;

	/* Compute transaction nonce. */
	if (crypto_hash_data_2(CRYPTO_KEY_HMAC_SHA256, C->snonce, 32,
	    C->cnonce, 32, C->seqnum)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Look up packet signing key. */
	if ((key = key_lookup(C->type)) == -1)
		goto err0;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->seqnum,
	    packetbuf, 1, key)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Store response code from server. */
	C->status = packetbuf[0];

	/* We're done! */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_transaction_start_write(NPC, machinenum, lastseq, seqnum):
 * Start a write transaction, presuming that ${lastseq} is the sequence
 * number of the last committed transaction; and return the sequence number
 * of the new transaction in ${seqnum}.
 */
int
storage_transaction_start_write(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, const uint8_t lastseq[32], uint8_t seqnum[32])
{

	return (storage_transaction_start(NPC, machinenum,
	    lastseq, seqnum, 0));
}

/**
 * storage_transaction_start_delete(NPC, machinenum, lastseq, seqnum):
 * As storage_transaction_start_delete, but s/write/delete/.
 */
int
storage_transaction_start_delete(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, const uint8_t lastseq[32], uint8_t seqnum[32])
{

	return (storage_transaction_start(NPC, machinenum,
	    lastseq, seqnum, 1));
}

/**
 * storage_transaction_start_fsck(NPC, machinenum, seqnum, whichkey):
 * Start a fsck transaction, and return the sequence number of the new
 * transaction in ${seqnum}.  Use the key specified by whichkey.
 */
int
storage_transaction_start_fsck(NETPACKET_CONNECTION * NPC,
    uint64_t machinenum, uint8_t seqnum[32], int whichkey)
{

	if (whichkey)
		return (storage_transaction_start(NPC, machinenum, NULL,
		    seqnum, 2));
	else
		return (storage_transaction_start(NPC, machinenum, NULL,
		    seqnum, 3));
}

/**
 * storage_transaction_checkpoint(machinenum, seqnum, ckptnonce, whichkey):
 * Create a checkpoint ${ckptnonce} in the current write transaction, which
 * has nonce ${seqnum}.  The value ${whichkey} is defined as in
 * storage_transaction_commit.
 */
int
storage_transaction_checkpoint(uint64_t machinenum, const uint8_t seqnum[32],
    const uint8_t ckptnonce[32], uint8_t whichkey)
{
	struct transaction_checkpoint_internal C;
	NETPACKET_CONNECTION * NPC;

	/* Initialize transaction structure. */
	C.machinenum = machinenum;
	memcpy(C.seqnum, seqnum, 32);
	memcpy(C.ckptnonce, ckptnonce, 32);
	C.whichkey = whichkey;
	C.done = 0;

	/* Open netpacket connection. */
	if ((NPC = netpacket_open(USERAGENT)) == NULL)
		goto err0;

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(NPC, callback_checkpoint_send, &C))
		goto err1;

	/* Wait until the checkpoint has been created (or failed). */
	if (network_spin(&C.done))
		goto err1;

	/* Close netpacket connection. */
	if (netpacket_close(NPC))
		goto err0;

	/* Report sequence number mismatch if necessary. */
	switch (C.status) {
	case 0:
		/* Success. */
		break;
	case 1:
		warn0("Sequence number mismatch creating checkpoint: "
		    "Run --fsck");
		goto err0;
	default:
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	netpacket_close(NPC);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_checkpoint_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct transaction_checkpoint_internal * C = cookie;

	/* Ask the server to create a checkpoint. */
	return (netpacket_transaction_checkpoint(NPC, C->machinenum,
	    C->whichkey, C->ckptnonce, C->seqnum,
	    callback_checkpoint_response));
}

static int
callback_checkpoint_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_checkpoint_internal * C = cookie;
	int key;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_CHECKPOINT_RESPONSE)
		goto err1;

	/* Look up packet signing key. */
	if ((key = key_lookup(C->whichkey)) == -1)
		goto err0;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->seqnum,
	    packetbuf, 33, key)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Verify that this is a response to the right request. */
	if (memcmp(&packetbuf[1], C->ckptnonce, 32))
		goto err1;

	/* Record status. */
	C->status = packetbuf[0];

	/* We're done! */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_transaction_commit(machinenum, seqnum, whichkey):
 * Commit the transaction ${seqnum} if it is the most recent uncommitted
 * transaction.  The value ${whichkey} specifies a key which should be used
 * to sign the commit request: 0 if the write key should be used, and 1 if
 * the delete key should be used.
 */
int
storage_transaction_commit(uint64_t machinenum, const uint8_t seqnum[32],
    uint8_t whichkey)
{
	struct transaction_commit_internal C;
	NETPACKET_CONNECTION * NPC;

	/* Initialize transaction structure. */
	C.machinenum = machinenum;
	memcpy(C.seqnum, seqnum, 32);
	C.whichkey = whichkey;

	/* Open netpacket connection. */
	if ((NPC = netpacket_open(USERAGENT)) == NULL)
		goto err0;

	/* Loop until the we error out or we get a "success" response. */
	do {
		/* Send a request and get a response. */
		C.done = 0;
		if (netpacket_op(NPC, callback_commit_send, &C))
			goto err1;

		/* Wait until we have a response or an error. */
		if (network_spin(&C.done))
			goto err1;

		/* Did we succeed? */
		if (C.status == 0)
			break;

		/* Sanity check status. */
		if (C.status != 1) {
			netproto_printerr(NETPROTO_STATUS_PROTERR);
			goto err1;
		}

		/* Give the server a chance to perform the commit. */
		sleep(1);
	} while (1);

	/* Close netpacket connection. */
	if (netpacket_close(NPC))
		goto err0;

	/* Success! */
	return (0);

err1:
	netpacket_close(NPC);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_commit_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct transaction_commit_internal * C = cookie;

	/* Ask the server to commit the transaction. */
	return (netpacket_transaction_trycommit(NPC, C->machinenum,
	    C->whichkey, C->seqnum, callback_commit_response));
}

static int
callback_commit_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_commit_internal * C = cookie;
	int key;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_TRYCOMMIT_RESPONSE)
		goto err1;

	/* Look up packet signing key. */
	if ((key = key_lookup(C->whichkey)) == -1)
		goto err0;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->seqnum,
	    packetbuf, 1, key)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Record status. */
	C->status = packetbuf[0];

	/* We're done! */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_transaction_commitfromcheckpoint(machinenum, whichkey):
 * If a write transaction is currently in progress and has a checkpoint,
 * commit it.  The value ${whichkey} specifies a key which should be used
 * to sign the commit request: 0 if the write key should be used, and 1 if
 * the delete key should be used.
 */
int
storage_transaction_commitfromcheckpoint(uint64_t machinenum,
    uint8_t whichkey)
{
	struct transaction_ischeckpointed_internal C;
	NETPACKET_CONNECTION * NPC;

	/* Initialize transaction structure. */
	C.machinenum = machinenum;
	C.whichkey = whichkey;

	/* Open netpacket connection. */
	if ((NPC = netpacket_open(USERAGENT)) == NULL)
		goto err0;

	/* Loop until the we error out or we get an answer. */
	do {
		/* Send a request and get a response. */
		C.done = 0;
		if (netpacket_op(NPC, callback_ischeckpointed_send, &C))
			goto err1;

		/* Wait until we have a response or an error. */
		if (network_spin(&C.done))
			goto err1;

		/* Did we succeed? */
		if ((C.status == 0) || (C.status == 1))
			break;

		/* Sanity check status. */
		if (C.status != 2) {
			netproto_printerr(NETPROTO_STATUS_PROTERR);
			goto err1;
		}

		/* Wait a bit and then ask again. */
		sleep(1);
	} while (1);

	/* Close netpacket connection. */
	if (netpacket_close(NPC))
		goto err0;

	/* If we have a checkpointed write transaction, commit it. */
	if (C.status == 1) {
		if (storage_transaction_commit(machinenum, C.tnonce,
		    whichkey))
			goto err0;
	}

	/* Success! */
	return (0);

err1:
	netpacket_close(NPC);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_ischeckpointed_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct transaction_ischeckpointed_internal * C = cookie;

	/* Generate a random request nonce. */
	if (crypto_entropy_read(C->nonce, 32))
		return (-1);

	/* Send the question. */
	return (netpacket_transaction_ischeckpointed(NPC, C->machinenum,
	    C->whichkey, C->nonce, callback_ischeckpointed_response));
}

static int
callback_ischeckpointed_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct transaction_ischeckpointed_internal * C = cookie;
	int key;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_ISCHECKPOINTED_RESPONSE)
		goto err1;

	/* Look up packet signing key. */
	if ((key = key_lookup(C->whichkey)) == -1)
		goto err0;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->nonce,
	    packetbuf, 1, key)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Record status. */
	C->status = packetbuf[0];
	memcpy(C->tnonce, &packetbuf[1], 32);

	/* We're done! */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}
