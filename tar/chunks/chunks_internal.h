#ifndef CHUNKS_INTERNAL_H_
#define CHUNKS_INTERNAL_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rwhashtab.h"

/* Chunk metadata structure was allocated by malloc(3). */
#define	CHDATA_MALLOC	((uint32_t)(1) << 30)
/* Chunk belongs to the current tape. */
#define CHDATA_CTAPE	((uint32_t)(1) << 31)
#define CHDATA_FLAGS	(0xc0000000)
#define CHDATA_ZLEN	(~CHDATA_FLAGS)

/* In-core chunk metadata structure. */
struct chunkdata {
	uint8_t hash[32];	/* HMAC of chunk. */
	uint32_t len;		/* Length of chunk. */
	uint32_t zlen_flags;	/* Compressed length of chunk | flags. */
	uint32_t nrefs;		/* Number of existing tapes using this. */
	uint32_t ncopies;	/* Number of copies of this chunk. */
};

/* In-core chunk metadata structure used by statstape. */
struct chunkdata_statstape {
	struct chunkdata d;
	uint32_t ncopies_ctape;	/* Used by chunks_stats only. */
};

/* Chunk statistics structure. */
struct chunkstats {
	uint64_t nchunks;	/* Number of chunks. */
	uint64_t s_len;		/* Total length of chunks. */
	uint64_t s_zlen;	/* Total compressed length of chunks. */
};

/**
 * chunks_directory_read(cachepath, dir, stats_unique, stats_all, stats_extra,
 *     mustexist, statstape):
 * Read stats_extra statistics (statistics on non-chunks which are stored)
 * and the chunk directory (if present) from "${cachepath}/directory" into
 * memory allocated and assigned to ${*dir}; and return a hash table
 * populated with struct chunkdata records.  Populate stats_all with
 * statistics for all the chunks listed in the directory (counting
 * multiplicity) and populate stats_unique with statistics reflecting the
 * unique chunks.  If ${mustexist}, error out if the directory does not exist.
 * If ${statstape}, allocate struct chunkdata_statstape records instead.
 */
RWHASHTAB * chunks_directory_read(const char *, void **,
    struct chunkstats *, struct chunkstats *, struct chunkstats *, int, int);

/**
 * chunks_directory_write(cachepath, HT, stats_extra, suff):
 * Write stats_extra statistics and the contents of the hash table ${HT} of
 * struct chunkdata records to a new chunk directory in
 * "${cachepath}/directory${suff}".
 */
int chunks_directory_write(const char *, RWHASHTAB *, struct chunkstats *,
    const char *);

/**
 * chunks_directory_free(HT, dir):
 * Free the hash table ${HT} of struct chunkdata records, all of its
 * elements, and ${dir}.
 */
void chunks_directory_free(RWHASHTAB *, void *);

/**
 * chunks_directory_commit(cachepath, osuff, nsuff):
 * If ${cachepath}/directory${osuff} exists, move it to
 * ${cachepath}/directory${nsuff} (replacing anything already there).
 */
int chunks_directory_commit(const char *, const char *, const char *);

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
void chunks_stats_add(struct chunkstats *, size_t, size_t, ssize_t);

/**
 * chunks_stats_addstats(to, from):
 * Add statistics in ${from} to the statistics in ${to}, storing the result
 * in ${to}.
 */
void chunks_stats_addstats(struct chunkstats *, struct chunkstats *);

/**
 * chunks_stats_printheader(stream, csv):
 * Print a header line for statistics to ${stream}, optionally in ${csv}
 * format.
 */
int chunks_stats_printheader(FILE *, int);

/**
 * chunks_stats_print(stream, stats, name, stats_extra, csv):
 * Print a line with ${name} and combined statistics from ${stats} and
 * ${stats_extra} to ${stream}, optionally in ${csv} format.
 */
int chunks_stats_print(FILE *, struct chunkstats *, const char *,
    struct chunkstats *, int);

#endif /* !CHUNKS_INTERNAL_H_ */
