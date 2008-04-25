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
 * Maximum number of bytes of file writes which are allowed to be pending
 * before storage_write_file will block.
 */
#define MAXPENDING_WRITEBYTES	(5 * 1024 * 1024)

struct storage_write_internal {
	/* Transaction parameters. */
	NETPACKET_CONNECTION * NPC;
	uint64_t machinenum;
	uint8_t nonce[32];

	/* Number of bytes of pending writes. */
	size_t nbytespending;
};

struct write_fexist_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in write_fexist. */
	uint8_t class;
	uint8_t name[32];
	uint8_t nonce[32];
	uint8_t status;
};

struct write_file_internal {
	/* Pointer to transaction to which this belongs. */
	struct storage_write_internal * S;

	/* General state information. */
	uint64_t machinenum;
	int done;

	/* Parameters used in write_file. */
	uint8_t class;
	uint8_t name[32];
	uint8_t nonce[32];
	uint8_t status;
	size_t flen;
	uint8_t * filebuf;
};

static sendpacket_callback callback_fexist_send;
static handlepacket_callback callback_fexist_response;
static sendpacket_callback callback_write_file_send;
static handlepacket_callback callback_write_file_response;

/**
 * storage_write_start(machinenum, lastseq, seqnum):
 * Start a write transaction, presuming that ${lastseq} is the the sequence
 * number of the last committed transaction, or zeroes if there is no
 * previous transaction; and store the sequence number of the new transaction
 * into ${seqnum}.
 */
STORAGE_W *
storage_write_start(uint64_t machinenum, const uint8_t lastseq[32],
    uint8_t seqnum[32])
{
	struct storage_write_internal * S;

	/* Allocate memory. */
	if ((S = malloc(sizeof(struct storage_write_internal))) == NULL)
		goto err0;

	/* Store machine number. */
	S->machinenum = machinenum;

	/* No pending writes so far. */
	S->nbytespending = 0;

	/* Open netpacket connection. */
	if ((S->NPC = netpacket_open()) == NULL)
		goto err1;

	/* Start a write transaction. */
	if (storage_transaction_start_write(S->NPC, machinenum,
	    lastseq, S->nonce))
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
 * storage_write_fexist(S, class, name):
 * Test if a file ${name} exists in class ${class}, as part of the write
 * transaction associated with the cookie ${S}; return 1 if the file
 * exists, 0 if not, and -1 on error.
 */
int
storage_write_fexist(STORAGE_W * S, char class, const uint8_t name[32])
{
	struct write_fexist_internal C;

	/* Initialize structure. */
	C.machinenum = S->machinenum;
	C.class = class;
	memcpy(C.name, name, 32);
	memcpy(C.nonce, S->nonce, 32);
	C.done = 0;

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(S->NPC, callback_fexist_send, &C))
		goto err0;

	/* Wait until the server has responded or we have failed. */
	if (network_spin(&C.done))
		goto err0;

	/* Parse status returned by server. */
	switch (C.status) {
	case 0:
		/* File does not exist. */
		break;
	case 1:
		/* File exists. */
		break;
	case 2:
		/* Bad nonce. */
		warn0("Transaction interrupted");
		goto err0;
	default:
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err0;
	}

	/* Success! */
	return (C.status);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_fexist_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct write_fexist_internal * C = cookie;

	/* Ask the server if the file exists. */
	return (netpacket_write_fexist(NPC, C->machinenum, C->class,
	    C->name, C->nonce, callback_fexist_response));
}

static int
callback_fexist_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct write_fexist_internal * C = cookie;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_WRITE_FEXIST_RESPONSE)
		goto err1;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->nonce,
	    packetbuf, 34, CRYPTO_KEY_AUTH_PUT)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Make sure that the packet corresponds to the right file. */
	if ((packetbuf[1] != C->class) ||
	    (memcmp(&packetbuf[2], C->name, 32)))
		goto err1;

	/* Record status code returned by server. */
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
 * storage_write_file(S, buf, len, class, name):
 * Write ${len} bytes from ${buf} to the file ${name} in class ${class} as
 * part of the write transaction associated with the cookie ${S}.
 */
int
storage_write_file(STORAGE_W * S, uint8_t * buf, size_t len,
    char class, const uint8_t name[32])
{
	struct write_file_internal  * C;

	/* Create write cookie. */
	if ((C = malloc(sizeof(struct write_file_internal))) == NULL)
		goto err0;
	C->S = S;
	C->machinenum = S->machinenum;
	C->class = class;
	memcpy(C->name, name, 32);
	memcpy(C->nonce, S->nonce, 32);
	C->done = 0;

	/* Sanity-check file length. */
	if (len > 262144 - CRYPTO_FILE_TLEN - CRYPTO_FILE_HLEN) {
		warn0("File is too large");
		goto err1;
	}
	C->flen = CRYPTO_FILE_HLEN + len + CRYPTO_FILE_TLEN;

	/* Allocate space for encrypted file. */
	if ((C->filebuf = malloc(C->flen)) == NULL)
		goto err1;

	/* Encrypt and hash file. */
	if (crypto_file_enc(buf, len, C->filebuf))
		goto err2;

	/* We're issuing a write operation. */
	S->nbytespending += C->flen;

	/*
	 * Make sure the pending operation queue isn't too large before we
	 * add yet another operation to it.
	 */
	while (S->nbytespending > MAXPENDING_WRITEBYTES) {
		if (network_select(1))
			goto err2;
	}

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(S->NPC, callback_write_file_send, C))
		goto err2;

	/* Success! */
	return (0);

err2:
	free(C->filebuf);
err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_write_file_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct write_file_internal * C = cookie;

	/* Write the file. */
	return (netpacket_write_file(NPC, C->machinenum, C->class, C->name,
	    C->filebuf, C->flen, C->nonce, callback_write_file_response));
}

static int
callback_write_file_response(void * cookie,
    NETPACKET_CONNECTION * NPC, int status, uint8_t packettype,
    const uint8_t * packetbuf, size_t packetlen)
{
	struct write_file_internal * C = cookie;

	(void)packetlen; /* UNUSED */
	(void)NPC; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err1;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_WRITE_FILE_RESPONSE)
		goto err2;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->nonce,
	    packetbuf, 34, CRYPTO_KEY_AUTH_PUT)) {
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
		/* This write operation is no longer pending. */
		C->S->nbytespending -= C->flen;
		break;
	case 1:
		warn0("Cannot store file: File already exists");
		goto err1;
	case 2:
		/* Bad nonce. */
		warn0("Transaction interrupted");
		goto err1;
	default:
		goto err2;
	}

	/* Free file buffer. */
	free(C->filebuf);

	/* Free write cookie. */
	free(C);

	/* Success! */
	return (0);

err2:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err1:
	free(C->filebuf);
	free(C);

	/* Failure! */
	return (-1);
}

/**
 * storage_write_flush(S):
 * Make sure all files written as part of the transaction associated with
 * the cookie ${S} have been safely stored in preparation for being committed.
 */
int
storage_write_flush(STORAGE_W * S)
{

	/* Wait until all pending writes have been completed. */
	while (S->nbytespending > 0) {
		if (network_select(1))
			goto err0;
	}

	/* Succes! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_write_end(S):
 * Make sure all files written as part of the transaction associated with
 * the cookie ${S} have been safely stored in preparation for being
 * committed; and close the transaction and free associated memory.
 */
int
storage_write_end(STORAGE_W * S)
{

	/* Flush any pending writes. */
	if (storage_write_flush(S))
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
 * storage_write_free(S):
 * Free any memory allocated as part of the write transaction associated with
 * the cookie ${S}; the transaction will not be committed.
 */
void
storage_write_free(STORAGE_W * S)
{

	/* Close netpacket connection. */
	netpacket_close(S->NPC);

	/* Free structure. */
	free(S);
}
