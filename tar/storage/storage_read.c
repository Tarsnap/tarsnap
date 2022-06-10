#include "platform.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "netpacket.h"
#include "netproto.h"
#include "rwhashtab.h"
#include "storage_internal.h"
#include "sysendian.h"
#include "warnp.h"

#include "storage.h"

struct storage_read_internal {
	NETPACKET_CONNECTION * NPC;
	RWHASHTAB * cache;
	struct read_file_cached * cache_mru;	/* Most recently used. */
	struct read_file_cached * cache_lru;	/* LRU of !evicted files. */
	size_t cachesz;
	size_t cachemaxsz;
	uint64_t machinenum;
};

struct read_file_cached {
	uint8_t classname[33];
	uint8_t * buf;				/* NULL if !inqueue. */
	size_t buflen;
	struct read_file_cached * next_lru;	/* Less recently used. */
	struct read_file_cached * next_mru;	/* More recently used. */
	int inqueue;
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
 * cache_lru_remove(S, CF):
 * Remove ${CF} from its current position in the LRU queue for ${S}.
 */
static void
cache_lru_remove(STORAGE_R * S, struct read_file_cached * CF)
{

	/* Sanity check: We should be in the queue. */
	assert(CF != NULL);
	assert(CF->inqueue);

	/* Our LRU file is now someone else's LRU file. */
	if (CF->next_mru != NULL)
		CF->next_mru->next_lru = CF->next_lru;
	else
		S->cache_mru = CF->next_lru;

	/* Our MRU file is now someone else's MRU file. */
	if (CF->next_lru != NULL)
		CF->next_lru->next_mru = CF->next_mru;
	else
		S->cache_lru = CF->next_mru;

	/* We're no longer in the queue. */
	CF->inqueue = 0;
	S->cachesz -= CF->buflen;

	/* We no longer have an MRU or LRU file. */
	CF->next_mru = NULL;
	CF->next_lru = NULL;
}

/**
 * cache_lru_add(S, CF):
 * Record ${CF} as the most recently used cached file in ${S}.
 */
static void
cache_lru_add(STORAGE_R * S, struct read_file_cached * CF)
{

	/* Sanity check: We should not be in the queue yet. */
	assert(CF->inqueue == 0);

	/* Nobody is more recently used than us... */
	CF->next_mru = NULL;

	/* ... the formerly MRU file is less recently used than us... */
	CF->next_lru = S->cache_mru;

	/* ... we're more recently used than any formerly MRU file... */
	if (CF->next_lru != NULL)
		CF->next_lru->next_mru = CF;

	/* ... and more recently used than nothing... */
	if (S->cache_lru == NULL)
		S->cache_lru = CF;

	/* ... and we're now the MRU file. */
	S->cache_mru = CF;

	/* We're now in the queue. */
	CF->inqueue = 1;
	S->cachesz += CF->buflen;
}

/**
 * cache_prune(S):
 * Prune the cache for ${S} down to size.
 */
static void
cache_prune(STORAGE_R * S)
{
	struct read_file_cached * CF;

	/* While the cache is too big... */
	while (S->cachesz > S->cachemaxsz) {
		/* Find the LRU cached file. */
		CF = S->cache_lru;

		/* Remove this file from the LRU list. */
		cache_lru_remove(S, CF);

		/* Free its data. */
		free(CF->buf);
		CF->buf = NULL;
		CF->buflen = 0;
	}
}

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

	/* Create a hash table for cached blocks. */
	if ((S->cache = rwhashtab_init(offsetof(struct read_file_cached,
	    classname), 33)) == NULL)
		goto err1;

	/* No cached data yet. */
	S->cache_lru = NULL;
	S->cache_mru = NULL;
	S->cachesz = 0;
	S->cachemaxsz = SIZE_MAX;

	/* Open netpacket connection. */
	if ((S->NPC = netpacket_open(USERAGENT)) == NULL)
		goto err2;

	/* Store machine number. */
	S->machinenum = machinenum;

	/* Success! */
	return (S);

err2:
	rwhashtab_free(S->cache);
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
	uint8_t classname[33];
	struct read_file_cached * CF;

	/* Prune the cache if necessary. */
	cache_prune(S);

	/* Is this file already marked as needing to be cached? */
	classname[0] = (uint8_t)class;
	memcpy(&classname[1], name, 32);
	if ((CF = rwhashtab_read(S->cache, classname)) != NULL) {
		/* If we're in the linked list, remove ourselves from it. */
		if (CF->inqueue)
			cache_lru_remove(S, CF);

		/* Insert ourselves at the head of the list. */
		cache_lru_add(S, CF);

		/* That's all we need to do. */
		goto done;
	}

	/* Allocate a structure. */
	if ((CF = malloc(sizeof(struct read_file_cached))) == NULL)
		goto err0;
	memcpy(CF->classname, classname, 33);
	CF->buf = NULL;
	CF->buflen = 0;
	CF->inqueue = 0;

	/* Add it to the cache. */
	if (rwhashtab_insert(S->cache, CF))
		goto err1;

	/* Add it to the LRU queue. */
	cache_lru_add(S, CF);

done:
	/* Success! */
	return (0);

err1:
	free(CF);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_read_set_cache_limit(S, size):
 * Set a limit of ${size} bytes on the cache associated with read cookie ${S}.
 */
void
storage_read_set_cache_limit(STORAGE_R * S, size_t size)
{

	/* Record the new size limit. */
	S->cachemaxsz = size;
}

/* Look for a file in the cache. */
static void
storage_read_cache_find(STORAGE_R * S, char class, const uint8_t name[32],
    uint8_t ** buf, size_t * buflen)
{
	uint8_t classname[33];
	struct read_file_cached * CF;

	/* Haven't found it yet. */
	*buf = NULL;
	*buflen = 0;

	/* Search for a cache entry. */
	classname[0] = (uint8_t)class;
	memcpy(&classname[1], name, 32);
	if ((CF = rwhashtab_read(S->cache, classname)) != NULL) {
		/* Found it! */
		*buf = CF->buf;
		*buflen = CF->buflen;
	}
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
	storage_read_cache_find(S, class, name, &cached_buf, &cached_buflen);
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
	storage_read_cache_find(S, class, name, &cached_buf, &cached_buflen);
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
	struct read_file_cached * CF;
	uint8_t classname[33];
	uint32_t filelen;
	int sc;
	int rc;

	(void)NPC; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_READ_FILE_RESPONSE)
		goto err1;

	/* Verify packet hmac. */
	switch (netpacket_hmac_verify(packettype, NULL,
	    packetbuf, packetlen - 32, CRYPTO_KEY_AUTH_GET)) {
	case 1:
		goto err1;
	case -1:
		goto err0;
	}

	/* Make sure that the packet corresponds to the right file. */
	if ((packetbuf[1] != C->class) ||
	    (memcmp(&packetbuf[2], C->name, 32)))
		goto err1;

	/* Extract status code and file length returned by server. */
	sc = packetbuf[0];
	filelen = be32dec(&packetbuf[34]);

	/* Verify packet integrity. */
	switch (sc) {
	case 0:
		if (packetlen != filelen + 70)
			goto err1;
		if (C->size != (uint32_t)(-1)) {
			if (filelen != C->size)
				goto err1;
		} else {
			if ((filelen < STORAGE_FILE_OVERHEAD) ||
			    (filelen > 262144))
				goto err1;
		}
		break;
	case 1:
	case 3:
		if ((packetlen != 70) || (filelen != 0))
			goto err1;
		break;
	case 2:
		if (packetlen != 70)
			goto err1;
		break;
	default:
		goto err1;
	}

	/* Decrypt file data if appropriate. */
	if (sc == 0) {
		/* Allocate a buffer if necessary. */
		if (C->size == (uint32_t)(-1)) {
			C->buflen = filelen - STORAGE_FILE_OVERHEAD;
			if ((C->buf = malloc(C->buflen)) == NULL)
				goto err0;
		}
		switch (crypto_file_dec(&packetbuf[38], C->buflen, C->buf)) {
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
			goto err0;
		}

		/* Should we cache this data? */
		classname[0] = C->class;
		memcpy(&classname[1], C->name, 32);
		if (((CF = rwhashtab_read(C->S->cache, classname)) != NULL) &&
		    (CF->inqueue != 0) && (CF->buf == NULL) && (sc == 0)) {
			/* Make a copy of this buffer if we can. */
			if ((CF->buf = malloc(C->buflen)) != NULL) {
				/* Copy in data and data length. */
				CF->buflen = C->buflen;
				memcpy(CF->buf, C->buf, C->buflen);

				/* We've got more data cached now. */
				C->S->cachesz += CF->buflen;
			}
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

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
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

/* Free a cache entry. */
static int
callback_cache_free(void * record, void * cookie)
{
	struct read_file_cached * CF = record;

	(void)cookie; /* UNUSED */

	/* Free the buffer and the structure. */
	free(CF->buf);
	free(CF);

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

	/* Free contents of cache. */
	rwhashtab_foreach(S->cache, callback_cache_free, NULL);

	/* Free cache. */
	rwhashtab_free(S->cache);

	/* Free memory. */
	free(S);
}
