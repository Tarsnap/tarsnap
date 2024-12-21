#ifndef CHUNKS_H_
#define CHUNKS_H_

#include <stddef.h>
#include <stdio.h>

#include "storage.h"

typedef struct chunks_read_internal	CHUNKS_R;
typedef struct chunks_write_internal	CHUNKS_W;
typedef struct chunks_delete_internal	CHUNKS_D;
typedef struct chunks_stats_internal	CHUNKS_S;

/**
 * chunks_read_init(S, maxchunksize):
 * Prepare to read chunks of maximum size ${maxchunksize} using the storage
 * layer cookie ${S}.
 */
CHUNKS_R * chunks_read_init(STORAGE_R *, size_t);

/**
 * chunks_read_cache(C, hash):
 * Using the read cookie ${C}, tell the storage layer to cache the chunk with
 * HMAC ${hash} after it is read.
 */
int chunks_read_cache(CHUNKS_R *, const uint8_t *);

/**
 * chunks_read_chunk(C, hash, len, zlen, buf, quiet):
 * Using the read cookie ${C}, read the chunk with HMAC ${hash}
 * into ${buf}; it should have length ${len} and compressed size ${zlen}.
 * If ${quiet}, don't print any warnings about corrupt or missing chunks.
 * Return 0 (success), 1 (ENOENT), 2 (corrupt), or -1 (error).
 */
int chunks_read_chunk(CHUNKS_R *, const uint8_t *, size_t, size_t,
    uint8_t *, int);

/**
 * chunks_read_free(C):
 * Close the read cookie ${C} and free any allocated memory.
 */
void chunks_read_free(CHUNKS_R *);

/**
 * chunks_write_start(cachepath, S, maxchunksize):
 * Start a write transaction using the cache directory ${cachepath} and the
 * storage layer cookie ${S} which will involve chunks of maximum size
 * ${maxchunksize}.
 */
CHUNKS_W * chunks_write_start(const char *, STORAGE_W *, size_t);

/**
 * chunks_write_chunk(C, hash, buf, buflen):
 * Write the chunk ${buf} of length ${buflen}, which has HMAC ${hash},
 * as part of the write transaction associated with the cookie ${C}.
 * Return the compressed size.
 */
ssize_t chunks_write_chunk(CHUNKS_W *, const uint8_t *, const uint8_t *,
    size_t);

/**
 * chunks_write_ispresent(C, hash):
 * If a chunk with hash ${hash} exists, return 0; otherwise, return 1.
 */
int chunks_write_ispresent(CHUNKS_W *, const uint8_t *);

/**
 * chunks_write_chunkref(C, hash):
 * If a chunk with hash ${hash} exists, mark it as being part of the write
 * transaction associated with the cookie ${C} and return 0.  If it
 * does not exist, return 1.
 */
int chunks_write_chunkref(CHUNKS_W *, const uint8_t *);

/**
 * chunks_write_extrastats(C, len):
 * Notify the chunk layer that non-chunked data of length ${len} has been
 * written directly to the storage layer; this information is used when
 * displaying archive statistics.
 */
void chunks_write_extrastats(CHUNKS_W *, size_t);

/**
 * chunks_write_extrastats_copy(C, direction):
 * Make a copy of the extra stats; if ${direction} is 0, copy from the real
 * stats to a copy; if 1, set the real stats to the copy.
 */
void chunks_write_extrastats_copy(CHUNKS_W *, size_t);

/**
 * chunks_write_printstats(stream, C, csv):
 * Print statistics for the write transaction associated with the cookie
 * ${C} to ${stream}, optionally in ${csv} format.
 */
int chunks_write_printstats(FILE *, CHUNKS_W *, int);

/**
 * chunks_write_checkpoint(C):
 * Create a checkpoint for the write transaction associated with the cookie
 * ${C}.
 */
int chunks_write_checkpoint(CHUNKS_W *);

/**
 * chunks_write_free(C):
 * End the write transaction associated with the cookie ${C}.
 */
void chunks_write_free(CHUNKS_W *);

/**
 * chunks_delete_start(cachepath, S):
 * Start a delete transaction using the cache directory ${cachepath} and the
 * storage layer cookie ${S}.
 */
CHUNKS_D * chunks_delete_start(const char *, STORAGE_D *);

/**
 * chunks_delete_getdirsz(C):
 * Return the number of entries in the chunks directory associated with ${C}.
 */
size_t chunks_delete_getdirsz(CHUNKS_D *);

/**
 * chunks_delete_chunk(C, hash):
 * Delete the chunk with HMAC ${hash} as part of the delete transaction
 * associated with the cookie ${C}.  Note that chunks are actually
 * removed from disk once they have been "deleted" by the same number of
 * transactions as they have been "written" by.
 */
int chunks_delete_chunk(CHUNKS_D *, const uint8_t *);

/**
 * chunks_delete_extrastats(C, len):
 * Notify the chunk layer that non-chunked data of length ${len} has been
 * deleted directly via the storage layer; this information is used when
 * displaying archive statistics.
 */
void chunks_delete_extrastats(CHUNKS_D *, size_t);

/**
 * chunks_delete_printstats(stream, C, name, csv):
 * Print statistics for the delete transaction associated with the cookie
 * ${C} to ${stream}, optionally in ${csv} format.  If ${name} is non-NULL,
 * use it to identify the archive being deleted.
 */
int chunks_delete_printstats(FILE *, CHUNKS_D *, const char *, int);

/**
 * chunks_delete_end(C):
 * Finish the delete transaction associated with the cookie ${C}.
 */
int chunks_delete_end(CHUNKS_D *);

/**
 * chunks_delete_free(C):
 * Terminate the delete transaction associated with the cookie ${C}.
 * (See chunks_write_free for details of what "terminate" means.)
 */
void chunks_delete_free(CHUNKS_D *);

/**
 * chunks_fsck_start(machinenum, cachepath):
 * Read the list of chunk files from the server and return a cookie which
 * can be used with chunks_stats_zeroarchive, chunks_stats_addchunk,
 * chunks_stats_extrastats, and other chunks_fsck_* calls.
 */
CHUNKS_S * chunks_fsck_start(uint64_t, const char *);

/**
 * chunks_fsck_archive_add(C):
 * Add the "current archive" statistics to the total chunk statistics.
 */
int chunks_fsck_archive_add(CHUNKS_S *);

/**
 * chunks_fsck_deletechunks(C, S):
 * Using the storage layer delete cookie ${S}, delete any chunks which have
 * not been recorded as being used by any archives.
 */
int chunks_fsck_deletechunks(CHUNKS_S *, STORAGE_D *);

/**
 * chunks_fsck_end(C):
 * Write out the chunk directory, and close the fscking cookie.
 */
int chunks_fsck_end(CHUNKS_S *);

/**
 * chunks_fsck_free(C):
 * Free the specified cookie; the fscking is being cancelled.
 */
#define chunks_fsck_free chunks_stats_free

/**
 * chunks_stats_init(cachepath):
 * Prepare for calls to other chunks_stats* functions.
 */
CHUNKS_S * chunks_stats_init(const char *);

/**
 * chunks_stats_getdirsz(C):
 * Return the number of entries in the chunks directory associated with ${C}.
 */
size_t chunks_stats_getdirsz(CHUNKS_S *);

/**
 * chunks_stats_printglobal(stream, C, csv):
 * Print global statistics relating to a set of archives, optionally in ${csv}
 * format.
 */
int chunks_stats_printglobal(FILE *, CHUNKS_S *, int);

/**
 * chunks_stats_printarchive(stream, C, name, csv):
 * Print accumulated statistics for an archive with the given name, optionally
 * in ${csv} format.
 */
int chunks_stats_printarchive(FILE *, CHUNKS_S *, const char *, int);

/**
 * chunks_stats_free(C):
 * No more calls will be made to chunks_stats* functions.
 */
void chunks_stats_free(CHUNKS_S *);

/**
 * chunks_transaction_checkpoint(cachepath):
 * Mark the pending checkpoint in the cache directory ${cachepath} as being
 * ready to commit from the perspective of the chunk layer.
 */
int chunks_transaction_checkpoint(const char *);

/**
 * chunks_transaction_commit(cachepath):
 * Commit the last finished transaction in the cache directory ${cachepath}
 * from the perspective of the chunk layer.
 */
int chunks_transaction_commit(const char *);

/*
 * Functions used by both _stats_ and _fsck_ codebases.
 */

/**
 * chunks_stats_zeroarchive(C):
 * Zero per-archive statistics.
 */
void chunks_stats_zeroarchive(CHUNKS_S *);

/**
 * chunks_stats_addchunk(C, hash, len, zlen):
 * Add the given chunk to the per-archive statistics.  If the chunk does not
 * exist, return 1.
 */
int chunks_stats_addchunk(CHUNKS_S *, const uint8_t *, size_t, size_t);

/**
 * chunks_stats_extrastats(C, len):
 * Notify the chunk layer that non-chunked data of length ${len} belongs to
 * the current archive.
 */
void chunks_stats_extrastats(CHUNKS_S *, size_t);

/*
 * Functions used by --initialize-cachedir and checking for a chunks directory.
 */

/**
 * chunks_initialize(cachepath):
 * Initialize the chunk directory (file) in ${cachepath}.  Return 0 on
 * success, -1 on error, and 1 if the file already exists.
 */
int chunks_initialize(const char *);

/**
 * chunks_directory_exists(cachepath):
 * Return 1 if the /directory file exists within ${cachepath}, 0 if it does
 * not, or -1 if there is an error.
 */
int chunks_directory_exists(const char *);

#endif /* !CHUNKS_H_ */
