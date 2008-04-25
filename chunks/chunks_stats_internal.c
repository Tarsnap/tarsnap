#include "bsdtar_platform.h"

#include <inttypes.h>
#include <stdio.h>

#include "storage.h"
#include "warnp.h"

#include "chunks_internal.h"

/**
 * chunks_stats_zero(stats):
 * Zero the provided set of statistics.
 */
void
chunks_stats_zero(struct chunkstats * stats)
{

	stats->nchunks = 0;
	stats->s_len = 0;
	stats->s_zlen = 0;
}

/**
 * chunks_stats_add(stats, len, zlen, copies):
 * Adjust ${stats} for the addition of ${copies} chunks each having length
 * ${len} and compressed length ${zlen}.
 */
void
chunks_stats_add(struct chunkstats * stats, size_t len, size_t zlen,
    ssize_t copies)
{

	stats->nchunks += copies;
	stats->s_len += (uint64_t)(len) * (int64_t)(copies);
	stats->s_zlen += (uint64_t)(zlen) * (int64_t)(copies);
}

/**
 * chunks_stats_addstats(to, from):
 * Add statistics in ${from} to the statistics in ${to}, storing the result
 * in ${to}.
 */
void
chunks_stats_addstats(struct chunkstats * to, struct chunkstats * from)
{

	to->nchunks += from->nchunks;
	to->s_len += from->s_len;
	to->s_zlen += from->s_zlen;
}

/**
 * chunks_stats_printheader(stream):
 * Print a header line for statistics to ${stream}.
 */
int
chunks_stats_printheader(FILE * stream)
{

#ifdef STATS_WITH_CHUNKS
	if (fprintf(stream, "%-25s  %12s  %15s  %15s\n",
	    "", "# of chunks", "Total size", "Compressed size") < 0) {
#else
	if (fprintf(stream, "%-32s  %15s  %15s\n",
	    "", "Total size", "Compressed size") < 0) {
#endif
		warnp("fprintf");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_stats_print(stream, stats, name, stats_extra):
 * Print a line with ${name} and combined statistics from ${stats} and
 * ${stats_extra} to ${stream}.
 */
int
chunks_stats_print(FILE * stream, struct chunkstats * stats,
    const char * name, struct chunkstats * stats_extra)
{
	struct chunkstats s;

	/* Compute sum of stats and stats_extra. */
	s.nchunks = stats->nchunks + stats_extra->nchunks;
	s.s_len = stats->s_len + stats_extra->s_len;
	s.s_zlen = stats->s_zlen + stats_extra->s_zlen;

	if (fprintf(stream,
#ifdef STATS_WITH_CHUNKS
	    "%-25s  %12" PRIu64 "  %15" PRIu64 "  %15" PRIu64 "\n",
	    name, s.nchunks, s.s_len,
#else
	    "%-32s  %15" PRIu64 "  %15" PRIu64 "\n",
	    name, s.s_len,
#endif
	    s.s_zlen + s.nchunks * STORAGE_FILE_OVERHEAD) < 0) {
		warnp("fprintf");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
