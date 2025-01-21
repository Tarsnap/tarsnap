#include "platform.h"

#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "ccache_internal.h"
#include "dirutil.h"
#include "multitape_internal.h"
#include "patricia.h"
#include "sysendian.h"
#include "warnp.h"

#include "ccache.h"

/* Cookie structure passed to callback_count and callback_write_*. */
struct ccache_write_internal {
	size_t N;	/* Number of records. */
	char * s;	/* File name. */
	FILE * f;	/* File handle. */
	char * sbuf;	/* Contains a NUL-terminated entry path. */
	size_t sbuflen;	/* Allocation size of sbuf. */
};

static int callback_count(void * cookie, uint8_t * s, size_t slen,
    void * rec);
static int callback_write_rec(void * cookie, uint8_t * s, size_t slen,
    void * rec);
static int callback_write_data(void * cookie, uint8_t * s, size_t slen,
    void * rec);

/* Should we skip this record? */
static int
skiprecord(struct ccache_record * ccr)
{

	/*
	 * Don't write an entry if there are no chunks and no trailer; if
	 * there's no data, we don't accomplish anything by having a record
	 * of the file in our cache.
	 */
	if ((ccr->nch == 0) && (ccr->tlen == 0))
		return (1);

	/*
	 * Don't write an entry if it hasn't been used recently; people often
	 * run several sets of archives covering different directories, so we
	 * don't want to drop cache entries as soon as they're not used in an
	 * archive, but we don't want to keep them for too long either so that
	 * we don't waste time / memory / disk space keeping track of a file
	 * which we'll never archive again.
	 */
	if (ccr->age > MAXAGE)
		return (1);

	/*
	 * Don't write an entry if it has negative mtime.  It is very unlikely
	 * to be correct, and if something is mangling a file's modification
	 * time there's too much of a risk that we'd rely on the modification
	 * time and incorrectly conclude that it hasn't been modified since
	 * the last time we looked at it.
	 */
	if (ccr->mtime < 0)
		return (1);

	/* This record looks reasonable; write it out. */
	return (0);
}

/* Callback to count the number of records which will be written. */
static int
callback_count(void * cookie, uint8_t * s, size_t slen, void * rec)
{
	struct ccache_write_internal * W = cookie;
	struct ccache_record * ccr = rec;

	(void)s; /* UNUSED */
	(void)slen; /* UNUSED */

	/* Count records we're not skipping. */
	if (!skiprecord(ccr))
		W->N += 1;

	/* Success! */
	return (0);
}

/* Callback to write a record and path suffix to disk. */
static int
callback_write_rec(void * cookie, uint8_t * s, size_t slen, void * rec)
{
	struct ccache_record_external ccre;
	struct ccache_write_internal * W = cookie;
	struct ccache_record * ccr = rec;
	size_t plen;

	/* Skip records which we don't want stored. */
	if (skiprecord(ccr))
		goto done;

	/* Sanity checks. */
	assert(slen <= UINT32_MAX);
	assert((ccr->size >= 0) && ((uintmax_t)ccr->size <= UINT64_MAX));
	assert((uintmax_t)ccr->mtime <= UINT64_MAX);
	assert((uintmax_t)ccr->ino <= UINT64_MAX);

	/* Figure out how much prefix is shared. */
	for (plen = 0; plen < slen && plen < W->sbuflen; plen++) {
		if (s[plen] != W->sbuf[plen])
			break;
	}

	/* Convert integers to portable format. */
	le64enc(ccre.ino, (uint64_t)ccr->ino);
	le64enc(ccre.size, (uint64_t)ccr->size);
	le64enc(ccre.mtime, (uint64_t)ccr->mtime);
	le64enc(ccre.nch, (uint64_t)ccr->nch);
	le32enc(ccre.tlen, (uint32_t)ccr->tlen);
	le32enc(ccre.tzlen, (uint32_t)ccr->tzlen);
	le32enc(ccre.prefixlen, (uint32_t)plen);
	le32enc(ccre.suffixlen, (uint32_t)(slen - plen));
	le32enc(ccre.age, (uint32_t)(ccr->age + 1));

	/* Write cache entry header to disk. */
	if (fwrite(&ccre, sizeof(ccre), 1, W->f) != 1)
		goto err0;

	/* Write path suffix to disk. */
	if (fwrite(s + plen, slen - plen, 1, W->f) != 1)
		goto err0;

	/* Enlarge last-path buffer if needed. */
	if (W->sbuflen < slen + 1) {
		free(W->sbuf);
		W->sbuflen = slen + 1;
		if ((W->sbuf = malloc(W->sbuflen)) == NULL) {
			W->sbuflen = 0;
			goto err0;
		}
		memcpy(W->sbuf, s, slen);
	} else
		memcpy(W->sbuf + plen, s + plen, slen - plen);
	W->sbuf[slen] = 0;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback to write chunk headers and compressed entry trailers to disk. */
static int
callback_write_data(void * cookie, uint8_t * s, size_t slen, void * rec)
{
	struct ccache_write_internal * W = cookie;
	struct ccache_record * ccr = rec;

	(void)s; /* UNUSED */
	(void)slen; /* UNUSED */

	/* Skip records which we don't want stored. */
	if (skiprecord(ccr))
		goto done;

	/* Write chunkheader records to disk, if any. */
	if (ccr->chp != NULL) {
		if (fwrite(ccr->chp, sizeof(struct chunkheader),
		    ccr->nch, W->f) != ccr->nch)
			goto err0;
	}

	/* Write compressed trailer to disk, if any. */
	if (ccr->ztrailer != NULL) {
		if (fwrite(ccr->ztrailer, ccr->tzlen, 1, W->f) != 1)
			goto err0;
	}

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * ccache_write(cache, path):
 * Write the given chunkification cache into the directory ${path}.
 */
int
ccache_write(CCACHE * cache, const char * path)
{
	struct ccache_internal * C = cache;
	struct ccache_write_internal W;
	uint8_t N[4];
	char * s_old;

	/* The caller must pass a file name to be written. */
	assert(path != NULL);

	/* Construct name of temporary cache file. */
	if (asprintf(&W.s, "%s/cache.new", path) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Open the cache file for writing. */
	if ((W.f = fopen(W.s, "w")) == NULL) {
		warnp("fopen(%s)", W.s);
		goto err1;
	}

	/*-
	 * We make three passes through the cache tree:
	 * 1. Counting the number of records which will be written to disk.
	 *    This is necessary since records in the cache which are too old
	 *    will not be written, but the on-disk cache format starts with
	 *    the number of records.
	 * 2. Writing the records and suffixes.
	 * 3. Writing the cached chunk headers and compressed entry trailers.
	 */

	/* Count the number of records which need to be written. */
	W.N = 0;
	if (patricia_foreach(C->tree, callback_count, &W)) {
		warnp("patricia_foreach");
		goto err2;
	}

	/* Check that we don't have too many cache records. */
	if (W.N > UINT32_MAX) {
		warn0("Programmer error: "
		    "The cache cannot contain more than 2^32-1 entries");
		goto err2;
	}

	/* Write the number of records to the file. */
	le32enc(N, (uint32_t)W.N);
	if (fwrite(N, 4, 1, W.f) != 1) {
		warnp("fwrite(%s)", W.s);
		goto err2;
	}

	/* Write the records and suffixes. */
	W.sbuf = NULL;
	W.sbuflen = 0;
	if (patricia_foreach(C->tree, callback_write_rec, &W)) {
		warnp("Error writing cache to %s", W.s);
		goto err2;
	}
	free(W.sbuf);

	/* Write the chunk headers and compressed entry trailers. */
	if (patricia_foreach(C->tree, callback_write_data, &W)) {
		warnp("Error writing cache to %s", W.s);
		goto err2;
	}

	/* Finish writing the file. */
	if (dirutil_fsync(W.f, W.s))
		goto err2;

	/* Close the file. */
	if (fclose(W.f))
		warnp("fclose");

	/* Construct the name of the old cache file. */
	if (asprintf(&s_old, "%s/cache", path) == -1) {
		warnp("asprintf");
		goto err1;
	}

	/* Delete the old file, if it exists. */
	if (unlink(s_old)) {
		if (errno != ENOENT) {
			warnp("unlink(%s)", s_old);
			free(s_old);
			goto err1;
		}
	}
	/* Move the new cache file into place. */
	if (rename(W.s, s_old)) {
		warnp("rename(%s, %s)", W.s, s_old);
		free(s_old);
		goto err1;
	}

	/* Free strings allocated by asprintf. */
	free(s_old);
	free(W.s);

	/* Success! */
	return (0);

err2:
	if (fclose(W.f))
		warnp("fclose");
err1:
	free(W.s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * ccache_remove(path):
 * Delete the chunkification cache from the directory ${path}.
 */
int
ccache_remove(const char * path)
{
	char * s;

	/* The caller must pass a file name to be deleted. */
	assert(path != NULL);

	/* Construct the name of the cache file. */
	if (asprintf(&s, "%s/cache", path) == -1) {
		warnp("asprintf");
		goto err1;
	}

	/* Delete the file if it exists. */
	if (unlink(s)) {
		if (errno != ENOENT) {
			warnp("unlink(%s)", s);
			goto err1;
		}
	}

	/* Free string allocated by asprintf. */
	free(s);

	/* Success! */
	return (0);

err1:
	free(s);

	/* Failure! */
	return (-1);
}
