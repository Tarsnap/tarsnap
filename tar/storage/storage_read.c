#include "platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "netpacket.h"
#include "netproto.h"
#include "storage_internal.h"
#include "sysendian.h"
#include "warnp.h"

#include "storage.h"
#include "storage_read_cache.h"

struct storage_read_internal {
	NETPACKET_CONNECTION * NPC;
	struct storage_read_cache * cache;
	uint64_t machinenum;
};

struct read_file_internal {
	int done;
	int status;
	uint8_t * buf;
	size_t buflen;
};

struct read_file_cookie {
	int (*callback)(void *, int, uint8_t *, size_t);
	void * cookie;
	struct storage_read_internal * S;
	uint64_t machinenum;
	uint8_t class;
	uint8_t name[32];
	uint32_t size;
	uint8_t * buf;
	size_t buflen;
};

static sendpacket_callback callback_read_file_send;
static handlepacket_callback callback_read_file_response;
static int callback_read_file(void *, int, uint8_t *, size_t);

/**
 * storage_read_init(machinenum):
 * Prepare for read operations.  Note that since reads are non-transactional,
 * this could be a no-op aside from storing the machine number.
 */
STORAGE_R *
storage_read_init(uint64_t machinenum)
{
	struct storage_read_internal * S;

	/* Allocate memory. */
	if ((S = malloc(sizeof(struct storage_read_internal))) == NULL)
		goto err0;

	/* Create the cache. */
	if ((S->cache = storage_read_cache_init()) == NULL)
		goto err1;

	/* Open netpacket connection. */
	if ((S->NPC = netpacket_open(USERAGENT)) == NULL)
		goto err2;

	/* Store machine number. */
	S->machinenum = machinenum;

	/* Success! */
	return (S);

err2:
	storage_read_cache_free(S->cache);
err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * storage_read_add_name_cache(S, class, name):
 * Add the file ${name} from class ${class} into the cache for the read cookie
 * ${S} returned from storage_read_init.  The data will not be fetched yet;
 * but any future fetch will look in the cache first and will store the block
 * in the cache if it needs to be fetched.
 */
int
storage_read_add_name_cache(STORAGE_R * S, char class, const uint8_t name[32])
{

	/* Pass request to the cache. */
	return (storage_read_cache_add_name(S->cache, class, name));
}

/**
 * storage_read_set_cache_limit(S, size):
 * Set a limit of ${size} bytes on the cache associated with read cookie ${S}.
 */
void
storage_read_set_cache_limit(STORAGE_R * S, size_t size)
{

	/* Pass request to the cache. */
	storage_read_cache_set_limit(S->cache, size);
}

/**
 * storage_read_file(S, buf, buflen, class, name):
 * Read the file ${name} from class ${class} using the read cookie ${S}
 * returned from storage_read_init into the buffer ${buf} of length ${buflen}.
 * Return 0 on success, 1 if the file does not exist; 2 if the file is not
 * ${buflen} bytes long or is corrupt; or -1 on error.
 */
int
storage_read_file(STORAGE_R * S, uint8_t * buf, size_t buflen,
    char class, const uint8_t name[32])
{
	struct read_file_internal C;
	uint8_t * cached_buf;
	size_t cached_buflen;

	/* Can we serve this from our cache? */
	storage_read_cache_find(S->cache, class, name, &cached_buf,
	    &cached_buflen);
	if (cached_buf != NULL) {
		if (buflen != cached_buflen) {
			/* Bad length. */
			C.status = 2;
			goto done;
		} else {
			/* Good length, copy data out. */
			C.status = 0;
			memcpy(buf, cached_buf, buflen);
			goto done;
		}
	}

	/* Initialize structure. */
	C.buf = buf;
	C.buflen = buflen;
	C.done = 0;

	/* Issue the request and spin until completion. */
	if (storage_read_file_callback(S, C.buf, C.buflen, class, name,
	    callback_read_file, &C))
		goto err0;
	if (network_spin(&C.done))
		goto err0;

done:
	/* Return status code from server. */
	return (C.status);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_read_file_alloc(S, buf, buflen, class, name):
 * Allocate a buffer and read the file ${name} from class ${class} using the
 * read cookie ${S} into it; set ${buf} to point at the buffer, and
 * ${buflen} to the length of the buffer.  Return 0, 1, 2, or -1 as per
 * storage_read_file.
 */
int
storage_read_file_alloc(STORAGE_R * S, uint8_t ** buf,
    size_t * buflen, char class, const uint8_t name[32])
{
	struct read_file_internal C;
	uint8_t * cached_buf;
	size_t cached_buflen;

	/* Can we serve this from our cache? */
	storage_read_cache_find(S->cache, class, name, &cached_buf,
	    &cached_buflen);
	if (cached_buf != NULL) {
		/* Allocate a buffer and copy data out. */
		if ((*buf = malloc(cached_buflen)) == NULL)
			goto err0;
		memcpy(*buf, cached_buf, cached_buflen);
		*buflen = cached_buflen;

		/* Data is good. */
		C.status = 0;
		goto done;
	}

	/* Initialize structure. */
	C.buf = NULL;
	C.buflen = 0;
	C.done = 0;

	/* Issue the request and spin until completion. */
	if (storage_read_file_callback(S, C.buf, C.buflen, class, name,
	    callback_read_file, &C))
		goto err0;
	if (network_spin(&C.done))
		goto err0;

	/* Store buffer and length if appropriate. */
	if (C.status == 0) {
		*buf = C.buf;
		*buflen = C.buflen;
	}

done:
	/* Return status code. */
	return (C.status);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_read_file_callback(S, buf, buflen, class, name, callback, cookie):
 * Read the file ${name} from class ${class} using the read cookie ${S}
 * returned from storage_read_init.  If ${buf} is non-NULL, then read the
 * file (which should be ${buflen} bytes in length) into ${buf}; otherwise
 * malloc a buffer.  Invoke ${callback}(${cookie}, status, b, blen) when
 * complete, where ${status} is 0, 1, 2, or -1 as per storage_read_file,
 * ${b} is the buffer into which the data was read (which will be ${buf} if
 * that value was non-NULL) and ${blen} is the length of the file.
 */
int
storage_read_file_callback(STORAGE_R * S, uint8_t * buf, size_t buflen,
    char class, const uint8_t name[32],
    int callback(void *, int, uint8_t *, size_t), void * cookie)
{
	struct read_file_cookie * C;

	/* Sanity-check file size if a buffer was provided. */
	if ((buf != NULL) && (buflen > 262144 - STORAGE_FILE_OVERHEAD)) {
		warn0("Programmer error: File too large");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct read_file_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->S = S;
	C->machinenum = S->machinenum;
	C->class = (uint8_t)class;
	memcpy(C->name, name, 32);

	/* Do we have a buffer? */
	if (buf != NULL) {
		C->buf = buf;
		C->buflen = buflen;
		C->size = (uint32_t)(buflen + STORAGE_FILE_OVERHEAD);
	} else {
		C->buf = NULL;
		C->buflen = 0;
		C->size = (uint32_t)(-1);
	}

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(S->NPC, callback_read_file_send, C))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_read_file_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct read_file_cookie * C = cookie;

	/* Ask the server to read the file. */
	return (netpacket_read_file(NPC, C->machinenum, C->class,
	    C->name, C->size, callback_read_file_response));
}

static int
callback_read_file_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct read_file_cookie * C = cookie;
	uint32_t filelen;
	int sc;
	int rc;

	(void)NPC; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err1;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_READ_FILE_RESPONSE)
		goto err2;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, NULL,
	    packetbuf, packetlen - 32, CRYPTO_KEY_AUTH_GET)) {
	case 1:
		goto err2;
	case -1:
		goto err1;
	}

	/* Make sure that the packet corresponds to the right file. */
	if ((packetbuf[1] != C->class) ||
	    (memcmp(&packetbuf[2], C->name, 32)))
		goto err2;

	/* Extract status code and file length returned by server. */
	sc = packetbuf[0];
	filelen = be32dec(&packetbuf[34]);

	/* Verify packet integrity. */
	switch (sc) {
	case 0:
		if (packetlen != filelen + 70)
			goto err2;
		if (C->size != (uint32_t)(-1)) {
			if (filelen != C->size)
				goto err2;
		} else {
			if ((filelen < STORAGE_FILE_OVERHEAD) ||
			    (filelen > 262144))
				goto err2;
		}
		break;
	case 1:
	case 3:
		if ((packetlen != 70) || (filelen != 0))
			goto err2;
		break;
	case 2:
		if (packetlen != 70)
			goto err2;
		break;
	default:
		goto err2;
	}

	/* Decrypt file data if appropriate. */
	if (sc == 0) {
		/* Allocate a buffer if necessary. */
		if (C->size == (uint32_t)(-1)) {
			C->buflen = filelen - STORAGE_FILE_OVERHEAD;
			if ((C->buf = malloc(C->buflen)) == NULL)
				goto err1;
		}
		switch (crypto_file_dec(&packetbuf[38], C->buflen, C->buf)) {
		case 0:
			/* Should we cache this data? */
			storage_read_cache_add_data(C->S->cache,
			    (char)C->class, C->name, C->buf, C->buflen);
			break;
		case 1:
			/* File is corrupt. */
			sc = 2;
			if (C->size == (uint32_t)(-1)) {
				free(C->buf);
				C->buf = NULL;
			}
			break;
		case -1:
			if (C->size == (uint32_t)(-1))
				free(C->buf);
			goto err1;
		}
	}

	/*
	 * If the user's tarsnap account balance is negative, print a warning
	 * message and then pass back a generic error status code.
	 */
	if (sc == 3) {
		warn0("Cannot read data from tarsnap server: "
		    "Account balance is not positive.");
		warn0("Please add more money to your tarsnap account");
		sc = -1;
	}

	/* Perform the callback. */
	rc = (C->callback)(C->cookie, sc, C->buf, C->buflen);

	/* Free the cookie. */
	free(C);

	/* Return result from callback. */
	return (rc);

err2:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err1:
	free(C);

	/* Failure! */
	return (-1);
}

/* Callback for storage_read_file and storage_read_file_alloc. */
static int
callback_read_file(void * cookie, int sc, uint8_t * buf, size_t buflen)
{
	struct read_file_internal * C = cookie;

	/* Record parameters. */
	C->status = sc;
	C->buf = buf;
	C->buflen = buflen;

	/* We're done. */
	C->done = 1;

	/* Success! */
	return (0);
}

/**
 * storage_read_free(S):
 * Close the read cookie ${S} and free any allocated memory.
 */
void
storage_read_free(STORAGE_R * S)
{

	/* Behave consistently with free(NULL). */
	if (S == NULL)
		return;

	/* Close netpacket connection. */
	netpacket_close(S->NPC);

	/* Free cache. */
	storage_read_cache_free(S->cache);

	/* Free memory. */
	free(S);
}
