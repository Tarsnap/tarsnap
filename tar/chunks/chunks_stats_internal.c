#include "platform.h"

#include <sys/types.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "asprintf.h"
#include "humansize.h"
#include "storage.h"
#include "tarsnap_opt.h"
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

	/*
	 * The value ${copies} may be negative, but since nchunks, s_len, and
	 * s_zlen are all of type uint64_t, casting a negative value to
	 * uint64_t will still yield the correct results, thanks to modulo-2^64
	 * arithmetic.
	 */
	stats->nchunks += (uint64_t)copies;
	stats->s_len += (uint64_t)(len) * (uint64_t)(copies);
	stats->s_zlen += (uint64_t)(zlen) * (uint64_t)(copies);
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
 * chunks_stats_printheader(stream, csv, print_nulls):
 * Print a header line for statistics to ${stream}, optionally in ${csv}
 * format.  If ${print_nulls} is non-zero, use '\0' for separators.
 */
int
chunks_stats_printheader(FILE * stream, int csv, int print_nulls)
{

	if (csv) {
		if (fprintf(stream, "%s", "Archive name") < 0) {
			warnp("fprintf");
			goto err0;
		}
		if (fprintf(stream, ",") < 0)
			goto err0;
#ifdef STATS_WITH_CHUNKS
		if (fprintf(stream, "%s", "# of chunks") < 0) {
			warnp("fprintf");
			goto err0;
		}
		if (fprintf(stream, ",") < 0)
			goto err0;
#endif
		if (fprintf(stream, "%s", "Total size") < 0) {
			warnp("fprintf");
			goto err0;
		}
		if (fprintf(stream, ",") < 0)
			goto err0;
		if (fprintf(stream, "%s", "Compressed size") < 0) {
			warnp("fprintf");
			goto err0;
		}
		if (fprintf(stream, "\n") < 0)
			goto err0;
	} else {
#ifdef STATS_WITH_CHUNKS
		if (fprintf(stream, "%-32s  %12s  %15s  %15s\n",
		    "", "# of chunks", "Total size", "Compressed size") < 0) {
#else
		if (fprintf(stream, "%-32s  %15s  %15s\n",
		    "", "Total size", "Compressed size") < 0) {
#endif
			warnp("fprintf");
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
 * chunks_stats_print(stream, stats, name, stats_extra, csv, print_nulls):
 * Print a line with ${name} and combined statistics from ${stats} and
 * ${stats_extra} to ${stream}, optionally in ${csv} format.  If ${print_nulls}
 * is non-zero, use '\0' as separators.
 */
int
chunks_stats_print(FILE * stream, struct chunkstats * stats,
    const char * name, struct chunkstats * stats_extra, int csv,
    int print_nulls)
{
	struct chunkstats s;
	char * s_lenstr, * s_zlenstr;

	/* Compute sum of stats and stats_extra. */
	s.nchunks = stats->nchunks + stats_extra->nchunks;
	s.s_len = stats->s_len + stats_extra->s_len;
	s.s_zlen = stats->s_zlen + stats_extra->s_zlen;

	/* Stringify values. */
	if (tarsnap_opt_humanize_numbers) {
		if ((s_lenstr = humansize(s.s_len)) == NULL)
			goto err0;
		if ((s_zlenstr = humansize(s.s_zlen +
		    s.nchunks * STORAGE_FILE_OVERHEAD)) == NULL)
			goto err1;
	} else {
		if (asprintf(&s_lenstr, "%" PRIu64, s.s_len) == -1) {
			warnp("asprintf");
			goto err0;
		}
		if (asprintf(&s_zlenstr, "%" PRIu64,
		    s.s_zlen + s.nchunks * STORAGE_FILE_OVERHEAD) == -1) {
			warnp("asprintf");
			goto err1;
		}
	}

	/* Print output line. */
	if (csv) {
		if (fprintf(stream, "%s", name) < 0) {
			warnp("fprintf");
			goto err2;
		}
		if (fprintf(stream, ",") <0)
			goto err2;
#ifdef STATS_WITH_CHUNKS
		if (fprintf(stream, "%" PRIu64, s.nchunks) < 0) {
			warnp("fprintf");
			goto err2;
		}
		if (fprintf(stream, ",") <0)
			goto err2;
#endif
		if (fprintf(stream, "%s", s_lenstr) < 0) {
			warnp("fprintf");
			goto err2;
		}
		if (fprintf(stream, ",") <0)
			goto err2;
		if (fprintf(stream, "%s", s_zlenstr) < 0) {
			warnp("fprintf");
			goto err2;
		}
		if (fprintf(stream, "\n") < 0)
			goto err2;
	} else {
		if (fprintf(stream,
#ifdef STATS_WITH_CHUNKS
		    "%-32s  %12" PRIu64 "  %15s  %15s\n",
		    name, s.nchunks,
#else
		    "%-32s  %15s  %15s\n",
		    name,
#endif
		    s_lenstr, s_zlenstr) < 0) {
			warnp("fprintf");
			goto err2;
		}
	}

	/* Free strings allocated by asprintf or humansize. */
	free(s_zlenstr);
	free(s_lenstr);

	/* Success! */
	return (0);

err2:
	free(s_zlenstr);
err1:
	free(s_lenstr);
err0:
	/* Failure! */
	return (-1);
}
