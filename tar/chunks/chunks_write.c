#include "bsdtar_platform.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "chunks_internal.h"
#include "hexify.h"
#include "rwhashtab.h"
#include "warnp.h"

#include "chunks.h"

struct chunks_write_internal {
	size_t maxlen;			/* Maximum chunk size. */
	uint8_t * zbuf;			/* Buffer for compression. */
	size_t zbuflen;			/* Length of zbuf. */
	RWHASHTAB * HT;			/* Hash table of struct chunkdata. */
	struct chunkdata * dir;		/* On-disk directory entries. */
	char * path;			/* Path to cache directory. */
	STORAGE_W * S;			/* Storage layer cookie. */
	struct chunkstats stats_total;	/* All archives, w/ multiplicity. */
	struct chunkstats stats_unique;	/* All archives, w/o multiplicity. */
	struct chunkstats stats_extra;	/* Extra (non-chunked) data. */
	struct chunkstats stats_tape;	/* This archive, w/ multiplicity. */
	struct chunkstats stats_new;	/* New chunks. */
	struct chunkstats stats_tapee;	/* Extra data in this archive. */
};

/**
 * chunks_write_start(cachepath, S, maxchunksize):
 * Start a write transaction using the cache directory ${cachepath} and the
 * storage layer cookie ${S} which will involve chunks of maximum size
 * ${maxchunksize}.
 */
CHUNKS_W *
chunks_write_start(const char * cachepath, STORAGE_W * S, size_t maxchunksize)
{
	struct chunks_write_internal * C;

	/* Sanity check. */
	if ((maxchunksize == 0) || (maxchunksize > SIZE_MAX / 2)) {
		warn0("Programmer error: maxchunksize invalid");
		goto err0;
	}

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct chunks_write_internal))) == NULL)
		return (NULL);

	/* Set length parameters. */
	C->maxlen = maxchunksize;
	C->zbuflen = C->maxlen + (C->maxlen / 1000) + 13;

	/* Allocate buffer for holding a compressed chunk. */
	if ((C->zbuf = malloc(C->zbuflen)) == NULL)
		goto err1;

	/* Record the storage cookie that we're using. */
	C->S = S;

	/* Create a copy of the path. */
	if ((C->path = strdup(cachepath)) == NULL)
		goto err2;

	/* Read the existing chunk directory (if one exists). */
	if ((C->HT = chunks_directory_read(cachepath, &C->dir,
	    &C->stats_unique, &C->stats_total, &C->stats_extra)) == NULL)
		goto err3;

	/* Zero "new chunks" and "this tape" statistics. */
	chunks_stats_zero(&C->stats_tape);
	chunks_stats_zero(&C->stats_new);
	chunks_stats_zero(&C->stats_tapee);

	/* Success! */
	return (C);

err3:
	free(C->path);
err2:
	free(C->zbuf);
err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);

}

/**
 * chunks_write_chunk(C, hash, buf, buflen):
 * Write the chunk ${buf} of length ${buflen}, which has HMAC ${hash},
 * as part of the write transaction associated with the cookie ${C}.
 * Return the compressed size.
 */
ssize_t
chunks_write_chunk(CHUNKS_W * C, const uint8_t * hash,
    const uint8_t * buf, size_t buflen)
{
	struct chunkdata * ch;
	uLongf zlen;
	char hashbuf[65];
	int rc;

	/* If the chunk is in ${C}->HT, return the compressed length. */
	if ((ch = rwhashtab_read(C->HT, hash)) != NULL) {
		chunks_stats_add(&C->stats_total, ch->len, ch->zlen, 1);
		chunks_stats_add(&C->stats_tape, ch->len, ch->zlen, 1);
		ch->ncopies += 1;
		if ((ch->flags & CHDATA_CTAPE) == 0) {
			ch->nrefs += 1;
			ch->flags |= CHDATA_CTAPE;
		}
		return (ch->zlen);
	}

	/* Compress the chunk. */
	zlen = C->zbuflen;
	if ((rc = compress2(C->zbuf, &zlen, buf, buflen, 9)) != Z_OK) {
		warn0("zlib error in compress2: %d", rc);
		goto err0;
	}

	/* Ask the storage layer to write the file for us. */
	if (storage_write_file(C->S, C->zbuf, zlen, 'c', hash)) {
		hexify(hash, hashbuf, 32);
		warnp("Error storing chunk %s", hashbuf);
		goto err0;
	}

	/* Allocate a new struct chunkdata... */
	if ((ch = malloc(sizeof(struct chunkdata))) == NULL)
		goto err0;

	/* ... fill in the chunk parameters... */
	memcpy(ch->hash, hash, 32);
	ch->len = buflen;
	ch->zlen = zlen;
	ch->nrefs = 1;
	ch->ncopies = 1;
	ch->flags = CHDATA_MALLOC | CHDATA_CTAPE;

	/* ... and insert it into the hash table. */
	if (rwhashtab_insert(C->HT, ch))
		goto err0;

	/* Update statistics. */
	chunks_stats_add(&C->stats_total, ch->len, ch->zlen, 1);
	chunks_stats_add(&C->stats_unique, ch->len, ch->zlen, 1);
	chunks_stats_add(&C->stats_tape, ch->len, ch->zlen, 1);
	chunks_stats_add(&C->stats_new, ch->len, ch->zlen, 1);

	/* Success! */
	return (zlen);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_write_ispresent(C, hash):
 * If a chunk with hash ${hash} exists, return 0; otherwise, return 1.
 */
int
chunks_write_ispresent(CHUNKS_W * C, const uint8_t * hash)
{

	if (rwhashtab_read(C->HT, hash) != NULL)
		return 0;
	else
		return 1;
}

/**
 * chunks_write_chunkref(C, hash):
 * If a chunk with hash ${hash} exists, mark it as being part of the write
 * transaction associated with the cookie ${C} and return 0.  If it
 * does not exist, return 1.
 */
int
chunks_write_chunkref(CHUNKS_W * C, const uint8_t * hash)
{
	struct chunkdata * ch;

	/*
	 * If the chunk is in ${C}->HT, mark it as being part of the
	 * transaction and return 0.
	 */
	if ((ch = rwhashtab_read(C->HT, hash)) != NULL) {
		chunks_stats_add(&C->stats_total, ch->len, ch->zlen, 1);
		chunks_stats_add(&C->stats_tape, ch->len, ch->zlen, 1);
		ch->ncopies += 1;
		if ((ch->flags & CHDATA_CTAPE) == 0) {
			ch->nrefs += 1;
			ch->flags |= CHDATA_CTAPE;
		}

		return (0);
	}

	/* If it does not exist, return 1. */
	return (1);
}

/**
 * chunks_write_extrastats(C, len):
 * Notify the chunk layer that non-chunked data of length ${len} has been
 * written directly to the storage layer; this information is used when
 * displaying archive statistics.
 */
void
chunks_write_extrastats(CHUNKS_W * C, size_t len)
{

	chunks_stats_add(&C->stats_extra, len, len, 1);
	chunks_stats_add(&C->stats_tapee, len, len, 1);
}

/**
 * chunks_write_printstats(stream, C):
 * Print statistics for the write transaction associated with the cookie
 * ${C} to ${stream}.
 */
int
chunks_write_printstats(FILE * stream, CHUNKS_W * C)
{

	/* Print header. */
	if (chunks_stats_printheader(stream))
		goto err0;

	/* Print the statistics we have. */
	if (chunks_stats_print(stream, &C->stats_total, "All archives",
	    &C->stats_extra))
		goto err0;
	if (chunks_stats_print(stream, &C->stats_unique, "  (unique data)",
	    &C->stats_extra))
		goto err0;
	if (chunks_stats_print(stream, &C->stats_tape, "This archive",
	    &C->stats_tapee))
		goto err0;
	if (chunks_stats_print(stream, &C->stats_new, "New data",
	    &C->stats_tapee))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_write_end(C):
 * Finish the write transaction associated with the cookie ${C}.
 */
int
chunks_write_end(CHUNKS_W * C)
{

	/* Write the new chunk directory. */
	if (chunks_directory_write(C->path, C->HT, &C->stats_extra))
		goto err1;

	/* Free the chunk hash table. */
	chunks_directory_free(C->HT, C->dir);

	/* Free memory. */
	free(C->zbuf);
	free(C->path);
	free(C);

	/* Success! */
	return (0);

err1:
	chunks_directory_free(C->HT, C->dir);
	free(C->zbuf);
	free(C->path);
	free(C);

	/* Failure! */
	return (-1);
}

/**
 * chunks_write_free(C):
 * Terminate the write transaction associated with the cookie ${C}.  For
 * a transaction which is not going to be committed, this is equivalent to
 * chunks_write_end; it should never be used if the transaction is going to
 * be committed.
 */
void
chunks_write_free(CHUNKS_W * C)
{

	/* Behave consistently with free(NULL). */
	if (C == NULL)
		return;

	/* Free the chunk hash table. */
	chunks_directory_free(C->HT, C->dir);

	/* Free memory. */
	free(C->zbuf);
	free(C->path);
	free(C);
}

