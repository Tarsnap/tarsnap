#include "bsdtar_platform.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunks_internal.h"
#include "hexify.h"
#include "rwhashtab.h"
#include "storage.h"

#include "chunks.h"

struct chunks_stats_internal {
	RWHASHTAB * HT;		/* Hash table of struct chunkdata_statstape. */
	struct chunkdata_statstape * dir;	/* On-disk directory entries. */
	char * cachepath;	/* Path to cache directory. */
	struct chunkstats stats_total;	/* All archives, w/ multiplicity. */
	struct chunkstats stats_unique;	/* All archives, w/o multiplicity. */
	struct chunkstats stats_extra;	/* Extra (non-chunked) data. */
	struct chunkstats stats_tape;	/* This archive, w/ multiplicity. */
	struct chunkstats stats_tapeu;	/* Data unique to this archive. */
	struct chunkstats stats_tapee;	/* Extra data in this archive. */
};

static int callback_zero(void *, void *);
static int callback_add(void *, void *);
static int callback_delete(void *, void *);

/**
 * callback_zero(rec, cookie):
 * Mark the struct chunkdata_statstape ${rec} as being not used in the current
 * archive.
 */
static int
callback_zero(void * rec, void * cookie)
{
	struct chunkdata_statstape * ch = rec;

	(void)cookie;	/* UNUSED */

	ch->d.zlen_flags &= ~CHDATA_CTAPE;
	ch->ncopies_ctape = 0;

	/* Success! */
	return (0);
}

/**
 * callback_add(rec, cookie):
 * Add the "current archive" statistics to the total chunk statistics.
 */
static int
callback_add(void * rec, void * cookie)
{
	struct chunkdata_statstape * ch = rec;

	(void)cookie;	/* UNUSED */

	ch->d.ncopies += ch->ncopies_ctape;
	if (ch->d.zlen_flags & CHDATA_CTAPE)
		ch->d.nrefs += 1;

	/* Success! */
	return (0);
}

/**
 * callback_delete(rec, cookie):
 * If the reference count of the struct chunkdata_statstape ${rec} is zero,
 * delete the chunk using the storage layer delete cookie ${cookie}.
 */
static int
callback_delete(void * rec, void * cookie)
{
	struct chunkdata_statstape * ch = rec;
	STORAGE_D * S = cookie;
	char hashbuf[65];

	if (ch->d.nrefs)
		goto done;

	hexify(ch->d.hash, hashbuf, 32);
	fprintf(stdout, "  Removing unreferenced chunk file: %s\n", hashbuf);
	if (storage_delete_file(S, 'c', ch->d.hash))
		goto err0;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_fsck_start(machinenum, cachepath):
 * Read the list of chunk files from the server and return a cookie which
 * can be used with chunks_stats_zeroarchive, chunks_stats_addchunk,
 * chunks_stats_extrastats, and other chunks_fsck_* calls.
 */
CHUNKS_S *
chunks_fsck_start(uint64_t machinenum, const char * cachepath)
{
	struct chunks_stats_internal * C;
	uint8_t * flist;
	size_t nfiles;
	size_t file;

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct chunks_stats_internal))) == NULL)
		goto err0;

	/* Create a copy of the path. */
	if ((C->cachepath = strdup(cachepath)) == NULL)
		goto err1;

	/* Get the list of chunk files from the server. */
	if (storage_directory_read(machinenum, 'c', 0, &flist, &nfiles))
		goto err2;

	/* Construct a chunkdata_statstape structure for each file. */
	if (nfiles > SIZE_MAX / sizeof(struct chunkdata_statstape)) {
		errno = ENOMEM;
		free(flist);
		goto err2;
	}
	if ((C->dir =
	    malloc(nfiles * sizeof(struct chunkdata_statstape))) == NULL) {
		free(flist);
		goto err2;
	}
	for (file = 0; file < nfiles; file++) {
		memcpy(C->dir[file].d.hash, &flist[file * 32], 32);
		C->dir[file].d.len = C->dir[file].d.zlen_flags = 0;
		C->dir[file].d.nrefs = C->dir[file].d.ncopies = 0;
	}

	/* Free the file list. */
	free(flist);

	/* Create an empty chunk directory. */
	C->HT = rwhashtab_init(offsetof(struct chunkdata, hash), 32);
	if (C->HT == NULL)
		goto err2;

	/* Insert the chunkdata structures we constructed above. */
	for (file = 0; file < nfiles; file++) {
		if (rwhashtab_insert(C->HT, &C->dir[file]))
			goto err3;
	}

	/* Zero statistics. */
	chunks_stats_zero(&C->stats_total);
	chunks_stats_zero(&C->stats_extra);
	chunks_stats_zero(&C->stats_unique);

	/* Success! */
	return (C);

err3:
	rwhashtab_free(C->HT);
err2:
	free(C->cachepath);
err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * chunks_fsck_archive_add(C):
 * Add the "current archive" statistics to the total chunk statistics.
 */
int
chunks_fsck_archive_add(CHUNKS_S * C)
{

	/* Add global "this archive" stats to global "total" stats. */
	chunks_stats_addstats(&C->stats_total, &C->stats_tape);
	chunks_stats_addstats(&C->stats_unique, &C->stats_tapeu);
	chunks_stats_addstats(&C->stats_extra, &C->stats_tapee);

	/* Add per-chunk "this archive" stats to per-chunk "total" stats. */
	return (rwhashtab_foreach(C->HT, callback_add, NULL));
}

/**
 * chunks_fsck_deletechunks(C, S):
 * Using the storage layer delete cookie ${S}, delete any chunks which have
 * not been recorded as being used by any archives.
 */
int
chunks_fsck_deletechunks(CHUNKS_S * C, STORAGE_D * S)
{

	/* Delete each chunk iff it has zero references. */
	return (rwhashtab_foreach(C->HT, callback_delete, S));
}

/**
 * chunks_fsck_end(C):
 * Write out the chunk directory, and close the fscking cookie.
 */
int
chunks_fsck_end(CHUNKS_S * C)
{
	int rc = 0;

	/* Write out the new chunk directory. */
	if (chunks_directory_write(C->cachepath, C->HT, &C->stats_extra,
	    ".tmp"))
		rc = -1;

	/* Free the chunk hash table. */
	chunks_directory_free(C->HT, C->dir);

	/* Free memory. */
	free(C->cachepath);
	free(C);

	/* Return status. */
	return (rc);
}

/**
 * chunks_stats_init(cachepath):
 * Prepare for calls to other chunks_stats* functions.
 */
CHUNKS_S *
chunks_stats_init(const char * cachepath)
{
	struct chunks_stats_internal * C;
	void * dir;

	/* Allocate memory. */
	if ((C = malloc(sizeof(struct chunks_stats_internal))) == NULL)
		goto err0;

	/* Create a copy of the path. */
	if ((C->cachepath = strdup(cachepath)) == NULL)
		goto err1;

	/* Read directory. */
	if ((C->HT = chunks_directory_read(cachepath, &dir,
	    &C->stats_unique, &C->stats_total, &C->stats_extra, 1, 1)) == NULL)
		goto err2;
	C->dir = dir;

	/* Success! */
	return (C);

err2:
	free(C->cachepath);
err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * chunks_stats_getdirsz(C):
 * Return the number of entries in the chunks directory associated with ${C}.
 */
size_t
chunks_stats_getdirsz(CHUNKS_S * C)
{

	/* Get the value from the hash table. */
	return (rwhashtab_getsize(C->HT));
}

/**
 * chunks_stats_printglobal(stream, C, csv):
 * Print global statistics relating to a set of archives, optionally in ${csv}
 * format.
 */
int
chunks_stats_printglobal(FILE * stream, CHUNKS_S * C, int csv)
{
	/* Print header. */
	if (chunks_stats_printheader(stream, csv))
		goto err0;

	/* Print the global statistics. */
	if (chunks_stats_print(stream, &C->stats_total, "All archives",
	    &C->stats_extra, csv))
		goto err0;
	if (chunks_stats_print(stream, &C->stats_unique, "  (unique data)",
	    &C->stats_extra, csv))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_stats_zeroarchive(C):
 * Zero per-archive statistics.
 */
void
chunks_stats_zeroarchive(CHUNKS_S * C)
{

	/* Zero global statistics. */
	chunks_stats_zero(&C->stats_tape);
	chunks_stats_zero(&C->stats_tapeu);
	chunks_stats_zero(&C->stats_tapee);

	/* Zero per-chunk statistics. */
	rwhashtab_foreach(C->HT, callback_zero, NULL);
}

/**
 * chunks_stats_addchunk(C, hash, len, zlen):
 * Add the given chunk to the per-archive statistics.  If the chunk does not
 * exist, return 1.
 */
int
chunks_stats_addchunk(CHUNKS_S * C, const uint8_t * hash,
    size_t len, size_t zlen)
{
	struct chunkdata_statstape * ch;

	/* If the chunk is not in ${S}->HT, error out. */
	if ((ch = rwhashtab_read(C->HT, hash)) == NULL)
		goto notpresent;

	/* Record the lengths if necessary. */
	if (ch->d.nrefs == 0 && ch->ncopies_ctape == 0) {
		ch->d.len = (uint32_t)len;
		ch->d.zlen_flags = (uint32_t)(zlen | (ch->d.zlen_flags & CHDATA_FLAGS));
	}

	/* Update "current tape" statistics. */
	chunks_stats_add(&C->stats_tape, len, zlen, 1);

	/* Update "data unique to this archive" statistics. */
	if ((ch->d.nrefs <= 1) && ((ch->d.zlen_flags & CHDATA_CTAPE) == 0))
		chunks_stats_add(&C->stats_tapeu, len, zlen, 1);

	/* Chunk is in current archive. */
	ch->ncopies_ctape += 1;
	ch->d.zlen_flags |= CHDATA_CTAPE;

	/* Success! */
	return (0);

notpresent:
	/* No such chunk exists. */
	return (1);
}

/**
 * chunks_stats_extrastats(C, len):
 * Notify the chunk layer that non-chunked data of length ${len} belongs to
 * the current archive.
 */
void
chunks_stats_extrastats(CHUNKS_S * C, size_t len)
{

	chunks_stats_add(&C->stats_tapee, len, len, 1);
}

/**
 * chunks_stats_printarchive(stream, C, name, csv):
 * Print accumulated statistics for an archive with the given name, optionally
 * in ${csv} format.
 */
int
chunks_stats_printarchive(FILE * stream, CHUNKS_S * C, const char * name,
    int csv)
{
	/* Print statistics for this archive. */
	if (chunks_stats_print(stream, &C->stats_tape, name,
	    &C->stats_tapee, csv))
		goto err0;
	if (chunks_stats_print(stream, &C->stats_tapeu, "  (unique data)",
	    &C->stats_tapee, csv))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_stats_free(C):
 * No more calls will be made to chunks_stats* functions.
 */
void
chunks_stats_free(CHUNKS_S * C)
{

	/* Behave consistently with free(NULL). */
	if (C == NULL)
		return;

	/* Free the chunk hash table. */
	chunks_directory_free(C->HT, C->dir);

	/* Free memory. */
	free(C->cachepath);
	free(C);
}

/**
 * chunks_initialize(const char * cachepath):
 * Initialize the chunk directory (file) in ${cachepath}.  Return 0 on
 * success, -1 on error, and 1 if the file already exists.
 */
int
chunks_initialize(const char * cachepath)
{
	RWHASHTAB * HT;
	struct chunkstats stats_extra;

	/* Bail if ${chunkpath}/directory already exists. */
 	switch (chunks_directory_exists(cachepath)) {
	case 0:
		break;
	case 1:
		goto fileexists;
	case -1:
		goto err0;
	}

	/* Allocate empty hash table, and zero stats. */
	if ((HT = rwhashtab_init(0, 1)) == NULL)
		goto err0;
	chunks_stats_zero(&stats_extra);

	/* Write empty directory file. */
	if (chunks_directory_write(cachepath, HT, &stats_extra, ""))
		goto err1;

	/* Free memory. */
	rwhashtab_free(HT);

	/* Success! */
	return (0);

err1:
	rwhashtab_free(HT);
err0:
	/* Failure! */
	return (-1);

fileexists:

	/* Failure! */
	return (1);
}
