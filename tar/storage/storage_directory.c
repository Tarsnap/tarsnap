#include "bsdtar_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "crypto_entropy.h"
#include "netpacket.h"
#include "netproto.h"
#include "storage_internal.h"
#include "sysendian.h"
#include "warnp.h"

#include "storage.h"

struct directory_read_internal {
	/* General state information. */
	uint64_t machinenum;
	int done;
	NETPACKET_CONNECTION * NPC;

	/* Parameters used in directory read. */
	uint8_t class;
	int key;
	uint8_t start[32];
	uint8_t nonce[32];
	uint8_t * flist;
	size_t nfiles;
	size_t nfiles_alloc;
};

static sendpacket_callback callback_getnonce_send;
static handlepacket_callback callback_getnonce_response;
static handlepacket_callback callback_directory_response;

/**
 * storage_directory_read(machinenum, class, key, flist, nfiles):
 * Fetch a sorted list of files in the specified class.  If ${key} is 0, use
 * NETPACKET_DIRECTORY requests (using the read key); otherwise, use
 * NETPACKET_DIRECTORY_D requests (using the delete key).  Return the list
 * and the number of files via ${flist} and ${nfiles} respectively.
 */
int
storage_directory_read(uint64_t machinenum, char class, int key,
    uint8_t ** flist, size_t * nfiles)
{
	struct directory_read_internal C;

	/* Open netpacket connection. */
	if ((C.NPC = netpacket_open(USERAGENT)) == NULL)
		goto err0;

	/* Initialize structure. */
	C.machinenum = machinenum;
	C.done = 0;
	C.class = class;
	C.key = key;
	memset(C.start, 0, 32);
	C.flist = NULL;
	C.nfiles = C.nfiles_alloc = 0;

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(C.NPC, callback_getnonce_send, &C))
		goto err2;

	/* Wait until we're done or we have failed. */
	if (network_spin(&C.done))
		goto err2;

	/* Return results. */
	*flist = C.flist;
	*nfiles = C.nfiles;

	/* Close netpacket connection. */
	if (netpacket_close(C.NPC))
		goto err1;

	/* Success! */
	return (0);

err2:
	netpacket_close(C.NPC);
err1:
	free(C.flist);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_getnonce_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct directory_read_internal * C = cookie;

	/* Ask the server to provide a transaction server nonce. */
	return (netpacket_transaction_getnonce(NPC, C->machinenum,
	    callback_getnonce_response));
}

static int
callback_getnonce_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct directory_read_internal * C = cookie;
	uint8_t cnonce[32];

	(void)packetlen; /* UNUSED */

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_TRANSACTION_GETNONCE_RESPONSE)
		goto err1;

	/* Generate a random client nonce. */
	if (crypto_entropy_read(cnonce, 32))
		goto err0;

	/* Compute operation nonce. */
	if (crypto_hash_data_2(CRYPTO_KEY_HMAC_SHA256, packetbuf, 32,
	    cnonce, 32, C->nonce)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Send a directory read request. */
	if (netpacket_directory(NPC, C->machinenum, C->class, C->start,
	    packetbuf, cnonce, C->key, callback_directory_response))
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
callback_directory_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct directory_read_internal * C = cookie;
	uint8_t * flist_new;
	size_t nfiles;
	size_t file;
	int i;

	/* Handle read errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_DIRECTORY_RESPONSE)
		goto err1;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, C->nonce,
	    packetbuf, packetlen - 32,
	    (C->key == 0) ? CRYPTO_KEY_AUTH_GET : CRYPTO_KEY_AUTH_DELETE)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Parse nfiles field. */
	nfiles = be32dec(&packetbuf[34]);

	/* Sanity-check parameters. */
	if ((packetbuf[0] > 3) ||
	    (packetbuf[1] != C->class) ||
	    (memcmp(&packetbuf[2], C->start, 32) != 0) ||
	    (nfiles > NETPACKET_DIRECTORY_RESPONSE_MAXFILES))
		goto err1;

	/* Sanity-check length. */
	if (packetlen != 70 + nfiles * 32)
		goto err1;

	/* Do we need to reallocate? */
	if (C->nfiles + nfiles > C->nfiles_alloc) {
		C->nfiles_alloc = 2 * C->nfiles + nfiles;
		if (C->nfiles_alloc > SIZE_MAX / 32) {
			errno = ENOMEM;
			goto err0;
		}
		if ((flist_new = malloc(C->nfiles_alloc * 32)) == NULL)
			goto err0;
		if (C->nfiles > 0)
			memcpy(flist_new, C->flist, C->nfiles * 32);
		free(C->flist);
		C->flist = flist_new;
	}

	/* Add files to list, while making sure that the files are ordered. */
	for (file = 0; file < nfiles; file++) {
		if (memcmp(C->start, &packetbuf[38 + file * 32], 32) > 0)
			goto err1;
		memcpy(C->start, &packetbuf[38 + file * 32], 32);
		memcpy(C->flist + (C->nfiles + file) * 32, C->start, 32);
		for (i = 31; i >= 0; i--)
			if (++(C->start[i]) != 0)
				break;
	}
	C->nfiles += nfiles;

	/* Are there more packets to come? */
	switch (packetbuf[0]) {
	case 0:
		/* No more files. */
		C->done = 1;
		break;
	case 1:
		/* More files to come. */
		if (netpacket_directory_readmore(NPC,
		    callback_directory_response))
			goto err0;
		break;
	case 2:
		/* We need to send another request. */
		if (netpacket_op(NPC, callback_getnonce_send, C))
			goto err0;
		break;
	case 3:
		/* Insufficient funds. */
		warn0("Cannot read list of archive fragments: "
		    "Account balance is not positive.");
		warn0("Please add more money to your tarsnap account");
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}
