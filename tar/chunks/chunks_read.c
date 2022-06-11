#include "platform.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "crypto.h"
#include "hexify.h"
#include "storage.h"
#include "warnp.h"

#include "chunks.h"

struct chunks_read_internal {
	size_t maxlen;		/* Maximum chunk size. */
	uint8_t * zbuf;		/* Buffer for compression. */
	size_t zbuflen;		/* Length of zbuf. */
	STORAGE_R * S;		/* Cookie for file read operations. */
};

/**
 * chunks_read_init(S, maxchunksize):
 * Prepare to read chunks of maximum size ${maxchunksize} using the storage
 * layer cookie ${S}.
 */
CHUNKS_R *
chunks_read_init(STORAGE_R * S, size_t maxchunksize)
{
	CHUNKS_R * C;

	/* Sanity check. */
	if ((maxchunksize == 0) || (maxchunksize > SIZE_MAX / 2)) {
		warn0("Programmer error: maxchunksize invalid");
		goto err0;
	}

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct chunks_read_internal))) == NULL)
		goto err0;

	/* Set length parameters. */
	C->maxlen = maxchunksize;
	C->zbuflen = C->maxlen + (C->maxlen / 1000) + 13;

	/* Allocate buffer for holding a compressed chunk. */
	if ((C->zbuf = malloc(C->zbuflen)) == NULL)
		goto err1;

	/* Record the storage cookie that we're using. */
	C->S = S;

	/* Success! */
	return (C);

err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * chunks_read_cache(C, hash):
 * Using the read cookie ${C}, tell the storage layer to cache the chunk with
 * HMAC ${hash} after it is read.
 */
int
chunks_read_cache(CHUNKS_R * C, const uint8_t * hash)
{

	/* Pass the message on to the storage layer. */
	return (storage_read_add_name_cache(C->S, 'c', hash));
}

/**
 * chunks_read_chunk(C, hash, len, zlen, buf, quiet):
 * Using the read cookie ${C}, read the chunk with HMAC ${hash}
 * into ${buf}; it should have length ${len} and compressed size ${zlen}.
 * If ${quiet}, don't print any warnings about corrupt or missing chunks.
 * Return 0 (success), 1 (ENOENT), 2 (corrupt), or -1 (error).
 */
int
chunks_read_chunk(CHUNKS_R * C, const uint8_t * hash, size_t len,
    size_t zlen, uint8_t * buf, int quiet)
{
	char hashbuf[65];
	uint8_t hash_actual[32];
	char hashbuf_actual[65];
	int rc;
	uLongf buflen;

	/* Sanity check ${len} and ${zlen} against parameters in ${C}. */
	if ((len > C->maxlen) || (zlen > C->zbuflen)) {
		if (quiet == 0)
			warn0("Chunk exceeds maximum size");
		goto corrupt;
	}

	/* Write the hash in hex for the benefit of error messages. */
	hexify(hash, hashbuf, 32);

	/* Ask the storage layer to read the file for us. */
	switch (storage_read_file(C->S, C->zbuf, zlen, 'c', hash)) {
	case -1:
		warnp("Error reading chunk %s", hashbuf);
		goto err0;
	case 0:
		/* File read successfully. */
		break;
	case 1:
		if (quiet == 0)
			warnp("Chunk not present: %s: Run --fsck", hashbuf);
		goto notpresent;
	case 2:
		if (quiet == 0)
			warn0("Chunk %s is corrupt", hashbuf);
		goto corrupt;
	}

	/* Decompress the chunk into ${buf}. */
	buflen = len;
	if ((rc = uncompress(buf, &buflen, C->zbuf, zlen)) != Z_OK) {
		if (quiet == 0) {
			switch (rc) {
			case Z_MEM_ERROR:
				errno = ENOMEM;
				warnp("Error decompressing chunk %s",
				    hashbuf);
				break;
			case Z_BUF_ERROR:
			case Z_DATA_ERROR:
				warn0("Error decompressing chunk %s: "
				    "chunk is corrupt", hashbuf);
				break;
			default:
				warn0("Programmer error: "
				    "Unexpected error code from "
				    "uncompress: %d", rc);
				break;
			}
		}
		goto corrupt;
	}

	/* Make sure the decompressed chunk length is correct. */
	if (buflen != len) {
		if (quiet == 0)
			warn0("Chunk %s has incorrect length"
			    " (%zd, expected %zd)",
			    hashbuf, (size_t)buflen, len);
		goto corrupt;
	}

	/* Make sure the decompressed chunk has correct HMAC. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_CHUNK, buf, len, hash_actual))
		goto err0;
	if (memcmp(hash, hash_actual, 32)) {
		hexify(hash_actual, hashbuf_actual, 32);
		if (quiet == 0)
			warn0("Chunk has incorrect hash (%s, expected %s)",
			    hashbuf_actual, hashbuf);
		goto corrupt;
	}

	/* Success! */
	return (0);

notpresent:
	/* ENOENT. */
	return (1);

corrupt:
	/* Chunk is corrupt. */
	return (2);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_read_free(C):
 * Close the read cookie ${C} and free any allocated memory.
 */
void
chunks_read_free(CHUNKS_R * C)
{

	/* Behave consistently with free(NULL). */
	if (C == NULL)
		return;

	/* Free memory. */
	free(C->zbuf);
	free(C);
}
