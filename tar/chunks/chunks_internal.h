#ifndef _CHUNKS_INTERNAL_H_
#define _CHUNKS_INTERNAL_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "rwhashtab.h"

/* Chunk metadata structure was allocated by malloc(3). */
#define	CHDATA_MALLOC	(1 << 0)
/* Chunk belongs to the current tape. */
#define CHDATA_CTAPE	(1 << 1)

/* In-core chunk metadata structure. */
struct chunkdata {
	uint8_t hash[32];	/* HMAC of chunk. */
	size_t len;		/* Length of chunk. */
	size_t zlen;		/* Compressed length of chunk. */
	size_t nrefs;		/* Number of existing tapes using this. */
	size_t ncopies;		/* Number of copies of this chunk. */
	size_t ncopies_ctape;	/* Used by chunks_fsck only. */
	int flags;		/* See CHDATA_* flags. */
};

/* Chunk statistics structure. */
struct chunkstats {
	uint64_t nchunks;	/* Number of chunks. */
	uint64_t s_len;		/* Total length of chunks. */
	uint64_t s_zlen;	/* Total compressed length of chunks. */
};

/**
 * chunks_directory_read(cachepath, dir, stats_unique, stats_all, stats_extra,
 *     mustexist):
 * Read stats_extra statistics (statistics on non-chunks which are stored)
 * and the chunk directory (if present) from "${cachepath}/directory" into
 * memory allocated and assigned to ${*dir}; and return a hash table
 * populated with struct chunkdata records.  Populate stats_all with
 * statistics for all the chunks listed in the directory (counting
 * multiplicity) and populate stats_unique with statistics reflecting the
 * unique chunks.  If ${mustexist}, error out if the directory does not exist.
 */
RWHASHTAB * chunks_directory_read(const char *, struct chunkdata **,
    struct chunkstats *, struct chunkstats *, struct chunkstats *, int);

/**
 * chunks_directory_write(cachepath, HT, stats_extra):
 * Write stats_extra statistics and the contents of the hash table ${HT} of
 * struct chunkdata records to a new chunk directory in
 * "${cachepath}/directory.tmp".
 */
int chunks_directory_write(const char *, RWHASHTAB *, struct chunkstats *);

/**
 * chunks_directory_free(HT, dir):
 * Free the hash table ${HT} of struct chunkdata records, all of its
 * elements, and ${dir}.
 */
void chunks_directory_free(RWHASHTAB *, struct chunkdata *);

/**
 * chunks_directory_commit(cachepath):
 * If ${cachepath}/directory.tmp exists, move it to ${cachepath}/directory
 * (replacing anything already there).
 */
int chunks_directory_commit(const char *);

/**
 * chunks_stats_zero(stats):
 * Zero the provided set of statistics.
 */
void chunks_stats_zero(struct chunkstats *);

/**
 * chunks_stats_add(stats, len, zlen, copies):
 * Adjust ${stats} for the addition of ${copies} chunks each having length
 * ${len} and compressed length ${zlen}.
 */
void chunks_stats_add(struct chunkstats *, size_t len, size_t zlen,
    ssize_t copies);

/**
 * chunks_stats_addstats(to, from):
 * Add statistics in ${from} to the statistics in ${to}, storing the result
 * in ${to}.
 */
void chunks_stats_addstats(struct chunkstats *, struct chunkstats *);

/**
 * chunks_stats_printheader(stream):
 * Print a header line for statistics to ${stream}.
 */
int chunks_stats_printheader(FILE *);

/**
 * chunks_stats_print(stream, stats, name, stats_extra):
 * Print a line with ${name} and combined statistics from ${stats} and
 * ${stats_extra} to ${stream}.
 */
int chunks_stats_print(FILE *, struct chunkstats *, const char *,
    struct chunkstats *);

#endif /* !_CHUNKS_INTERNAL_H_ */
