#include "bsdtar_platform.h"

#include <stdlib.h>
#include <string.h>

#include "chunks_internal.h"
#include "rwhashtab.h"
#include "storage.h"
#include "warnp.h"

#include "chunks.h"

struct chunks_delete_internal {
	RWHASHTAB * HT;			/* Hash table of struct chunkdata. */
	void * dir;			/* On-disk directory entries. */
	char * path;			/* Path to cache directory. */
	STORAGE_D * S;			/* Storage layer cookie. */
	struct chunkstats stats_total;	/* All archives, w/ multiplicity. */
	struct chunkstats stats_unique;	/* All archives, w/o multiplicity. */
	struct chunkstats stats_extra;	/* Extra (non-chunked) data. */
	struct chunkstats stats_tape;	/* This archive, w/ multiplicity. */
	struct chunkstats stats_freed;	/* Chunks being deleted. */
	struct chunkstats stats_tapee;	/* Extra data in this archive. */
};

/**
 * chunks_delete_start(cachepath, S):
 * Start a delete transaction using the cache directory ${cachepath} and the
 * storage layer cookie ${S}.
 */
CHUNKS_D *
chunks_delete_start(const char * cachepath, STORAGE_D * S)
{
	struct chunks_delete_internal * C;

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct chunks_delete_internal))) == NULL)
		goto err0;

	/* Record the storage cookie that we're using. */
	C->S = S;

	/* Create a copy of the path. */
	if ((C->path = strdup(cachepath)) == NULL)
		goto err1;

	/* Read the existing chunk directory. */
	if ((C->HT = chunks_directory_read(cachepath, &C->dir,
	    &C->stats_unique, &C->stats_total, &C->stats_extra, 0, 0)) == NULL)
		goto err2;

	/* Zero "new chunks" and "this tape" statistics. */
	chunks_stats_zero(&C->stats_tape);
	chunks_stats_zero(&C->stats_freed);
	chunks_stats_zero(&C->stats_tapee);

	/* Success! */
	return (C);

err2:
	free(C->path);
err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * chunks_delete_getdirsz(C):
 * Return the number of entries in the chunks directory associated with ${C}.
 */
size_t
chunks_delete_getdirsz(CHUNKS_D * C)
{

	/* Get the value from the hash table. */
	return (rwhashtab_getsize(C->HT));
}

/**
 * chunks_delete_chunk(C, hash):
 * Delete the chunk with HMAC ${hash} as part of the delete transaction
 * associated with the cookie ${C}.  Note that chunks are actually
 * removed from disk once they have been "deleted" by the same number of
 * transactions as they have been "written" by.
 */
int
chunks_delete_chunk(CHUNKS_D * C, const uint8_t * hash)
{
	struct chunkdata * ch;

	/* If the chunk is not in ${C}->HT, error out. */
	if ((ch = rwhashtab_read(C->HT, hash)) == NULL) {
		warn0("Chunk is missing or directory is corrupt");
		goto err0;
	}

	/* Update statistics. */
	chunks_stats_add(&C->stats_total, ch->len,
	    ch->zlen_flags & CHDATA_ZLEN, -1);
	chunks_stats_add(&C->stats_tape, ch->len,
	    ch->zlen_flags & CHDATA_ZLEN, 1);
	ch->ncopies -= 1;

	/* If the chunk is not marked as CHDATA_CTAPE... */
	if ((ch->zlen_flags & CHDATA_CTAPE) == 0) {
		/* ... add that flag... */
		ch->zlen_flags |= CHDATA_CTAPE;

		/* ... decrement the reference counter... */
		ch->nrefs -= 1;

		/* ... and delete the chunk if the refcount is now zero. */
		if (ch->nrefs == 0) {
			chunks_stats_add(&C->stats_unique, ch->len,
			    ch->zlen_flags & CHDATA_ZLEN, -1);
			chunks_stats_add(&C->stats_freed, ch->len,
			    ch->zlen_flags & CHDATA_ZLEN, 1);

			if (storage_delete_file(C->S, 'c', hash))
				goto err0;
		}
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_delete_extrastats(C, len):
 * Notify the chunk layer that non-chunked data of length ${len} has been
 * deleted directly via the storage layer; this information is used when
 * displaying archive statistics.
 */
void
chunks_delete_extrastats(CHUNKS_D * C, size_t len)
{

	chunks_stats_add(&C->stats_extra, len, len, -1);
	chunks_stats_add(&C->stats_tapee, len, len, 1);
}

/**
 * chunks_delete_printstats(stream, C, name):
 * Print statistics for the delete transaction associated with the cookie
 * ${C} to ${stream}.  If ${name} is non-NULL, use it to identify the archive
 * being deleted.
 */
int
chunks_delete_printstats(FILE * stream, CHUNKS_D * C, const char * name)
{

	/* If we don't have an archive name, call it "This archive". */
	if (name == NULL)
		name = "This archive";

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
	if (chunks_stats_print(stream, &C->stats_tape, name,
	    &C->stats_tapee))
		goto err0;
	if (chunks_stats_print(stream, &C->stats_freed, "Deleted data",
	    &C->stats_tapee))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_delete_end(C):
 * Finish the delete transaction associated with the cookie ${C}.
 */
int
chunks_delete_end(CHUNKS_D * C)
{

	/* Write the new chunk directory. */
	if (chunks_directory_write(C->path, C->HT, &C->stats_extra, ".tmp"))
		goto err1;

	/* Free the chunk hash table. */
	chunks_directory_free(C->HT, C->dir);

	/* Free memory. */
	free(C->path);
	free(C);

	/* Success! */
	return (0);

err1:
	chunks_directory_free(C->HT, C->dir);
	free(C->path);
	free(C);

	/* Failure! */
	return (-1);
}

/**
 * chunks_delete_free(C):
 * Terminate the delete transaction associated with the cookie ${C}.
 * (See chunks_write_free for details of what "terminate" means.)
 */
void
chunks_delete_free(CHUNKS_D * C)
{

	/* Behave consistently with free(NULL). */
	if (C == NULL)
		return;

	/* Free the chunk hash table. */
	chunks_directory_free(C->HT, C->dir);

	/* Free memory. */
	free(C->path);
	free(C);
}
