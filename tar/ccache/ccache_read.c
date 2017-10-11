#include "bsdtar_platform.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "ccache_internal.h"
#include "multitape_internal.h"
#include "patricia.h"
#include "sysendian.h"
#include "warnp.h"

#include "ccache.h"

/* Cookie structure passed to read_rec and callback_read_data. */
struct ccache_read_internal {
	size_t N;	/* Number of records. */
	char * s;	/* File name. */
	FILE * f;	/* File handle. */
	uint8_t * sbuf;	/* Contains a NUL-terminated entry path. */
	size_t sbuflen;	/* Allocation size of sbuf. */
	size_t slen;	/* Length of string currently stored in sbuf. */
	size_t datalen;	/* Sum of chunk header and trailer lengths. */
	uint8_t * data;	/* Mmapped data. */
};

static struct ccache_record * read_rec(void * cookie);
static int callback_read_data(void * cookie, uint8_t * s, size_t slen,
    void * rec);
static int callback_free(void * cookie, uint8_t * s, size_t slen,
    void * rec);

/* Read a cache record. */
static struct ccache_record *
read_rec(void * cookie)
{
	struct ccache_record_external ccre;
	struct ccache_read_internal * R = cookie;
	struct ccache_record * ccr;
	size_t prefixlen, suffixlen;
	uint8_t * sbuf_new;

	/* Read a struct ccache_record_external. */
	if (fread(&ccre, sizeof(ccre), 1, R->f) != 1) {
		if (ferror(R->f))
			warnp("Error reading cache: %s", R->s);
		else
			warn0("Error reading cache: %s", R->s);
		goto err0;
	}

	/* Allocate memory for a record. */
	if ((ccr = malloc(sizeof(struct ccache_record))) == NULL)
		goto err0;

	/* Decode record. */
	ccr->ino = (ino_t)le64dec(ccre.ino);
	ccr->size = (off_t)le64dec(ccre.size);
	ccr->mtime = (time_t)le64dec(ccre.mtime);
	ccr->nch = (size_t)le64dec(ccre.nch);
	ccr->tlen = le32dec(ccre.tlen);
	ccr->tzlen = le32dec(ccre.tzlen);
	prefixlen = le32dec(ccre.prefixlen);
	suffixlen = le32dec(ccre.suffixlen);
	ccr->age = (int)le32dec(ccre.age);

	/* Zero other fields. */
	ccr->nchalloc = 0;
	ccr->chp = NULL;
	ccr->ztrailer = NULL;
	ccr->flags = 0;

	/* Sanity check some fields. */
	if (le64dec(ccre.nch) > (uint64_t)SIZE_MAX) {
		warn0("Cache file is corrupt or too large for this "
		    "platform: %s", R->s);
		goto err1;
	}
	if ((prefixlen == 0 && suffixlen == 0) ||
	    (ccr->nch > SIZE_MAX / sizeof(struct chunkheader)) ||
	    (ccr->nch == 0 && ccr->tlen == 0) ||
	    (ccr->tlen == 0 && ccr->tzlen != 0) ||
	    (ccr->tlen != 0 && ccr->tzlen == 0) ||
	    (ccr->age == INT_MAX))
		goto err2;

	/*
	 * The prefix length must be <= the length of the previous path; and
	 * the prefix length + suffix length must not overflow.
	 */
	if ((prefixlen > R->slen) || (prefixlen > prefixlen + suffixlen))
		goto err2;

	/* Make sure we have enough space for the entry path. */
	if (prefixlen + suffixlen > R->sbuflen) {
		sbuf_new = realloc(R->sbuf, prefixlen + suffixlen);
		if (sbuf_new == NULL)
			goto err1;
		R->sbuf = sbuf_new;
		R->sbuflen = prefixlen + suffixlen;
	}

	/* Read the entry path suffix. */
	if (fread(R->sbuf + prefixlen, suffixlen, 1, R->f) != 1) {
		if (ferror(R->f))
			warnp("Error reading cache: %s", R->s);
		else
			warn0("Error reading cache: %s", R->s);
		goto err1;
	}
	R->slen = prefixlen + suffixlen;

	/* Add chunk header and trailer data lengths to datalen. */
	R->datalen += ccr->tzlen;
	if (R->datalen < ccr->tzlen)
		goto err2;
	R->datalen += ccr->nch * sizeof(struct chunkheader);
	if (R->datalen < ccr->nch * sizeof(struct chunkheader))
		goto err2;

	/* Success! */
	return (ccr);

err2:
	warn0("Cache file is corrupt: %s", R->s);
err1:
	free(ccr);
err0:
	/* Failure! */
	return (NULL);
}

/* Read chunk headers and compressed entry trailer if appropriate. */
static int
callback_read_data(void * cookie, uint8_t * s, size_t slen, void * rec)
{
	struct ccache_read_internal * R = cookie;
	struct ccache_record * ccr = rec;

	(void)s;	/* UNUSED */
	(void)slen;	/* UNUSED */

	/* Read chunk headers, if present. */
	if (ccr->nch) {
		ccr->chp = (struct chunkheader *)(R->data);
		R->data += ccr->nch * sizeof(struct chunkheader);
	}

	/* Read compressed trailer, if present. */
	if (ccr->tzlen) {
		ccr->ztrailer = R->data;
		R->data += ccr->tzlen;
	}

	/* Success! */
	return (0);
}

/* Callback to free a ccache_record structure. */
static int
callback_free(void * cookie, uint8_t * s, size_t slen, void * rec)
{
	struct ccache_record * ccr = rec;

	(void)cookie;	/* UNUSED */
	(void)s;	/* UNUSED */
	(void)slen;	/* UNUSED */

	/* Free chunkheader records, if they weren't mmapped. */
	if (ccr->nchalloc)
		free(ccr->chp);

	/* Free trailer, if not mmapped. */
	if (ccr->flags & CCR_ZTRAILER_MALLOC)
		free(ccr->ztrailer);

	/* Free cache record. */
	free(ccr);

	/* Success! */
	return (0);
}

/**
 * ccache_read(path):
 * Read the chunkification cache (if present) from the directory ${path};
 * return a Patricia tree mapping absolute paths to cache entries.
 */
CCACHE *
ccache_read(const char * path)
{
	struct ccache_internal * C;
	struct ccache_read_internal R;
	struct ccache_record * ccr;
#ifdef HAVE_MMAP
	struct stat sb;
	off_t fpos;
	long int pagesize;
#endif
	size_t i;
	uint8_t N[4];

	/* The caller must pass a file name to be read. */
	assert(path != NULL);

	/* Allocate memory for the cache. */
	if ((C = malloc(sizeof(struct ccache_internal))) == NULL)
		goto err0;
	memset(C, 0, sizeof(struct ccache_internal));

	/* Create a Patricia tree to store cache entries. */
	if ((C->tree = patricia_init()) == NULL)
		goto err1;

	/* Construct the name of cache file. */
	if (asprintf(&R.s, "%s/cache", path) == -1) {
		warnp("asprintf");
		goto err2;
	}

	/* Open the cache file. */
	if ((R.f = fopen(R.s, "r")) == NULL) {
		/* ENOENT isn't an error. */
		if (errno != ENOENT) {
			warnp("fopen(%s)", R.s);
			goto err3;
		}

		/* No cache exists on disk; return an empty cache. */
		goto emptycache;
	}

	/**
	 * We read the cache file in three steps:
	 * 1. Read a little-endian uint32_t which indicates the number of
	 *    records in the cache file.
	 * 2. Read N (record, path suffix) pairs and insert them into a
	 *    Patricia tree.
	 * 3. Iterate through the tree and read chunk headers and compressed
	 *    entry trailers.
	 */

	/* Read the number of cache entries. */
	if (fread(N, 4, 1, R.f) != 1) {
		if (ferror(R.f))
			warnp("Error reading cache: %s", R.s);
		else
			warn0("Error reading cache: %s", R.s);
		goto err4;
	}
	R.N = le32dec(N);

	/* Read N (record, path suffix) pairs. */
	R.sbuf = NULL;
	R.sbuflen = R.slen = R.datalen = 0;
	for (i = 0; i < R.N; i++) {
		if ((ccr = read_rec(&R)) == NULL)
			goto err5;
		if (patricia_insert(C->tree, R.sbuf, R.slen, ccr))
			goto err5;
		C->chunksusage += ccr->nch * sizeof(struct chunkheader);
		C->trailerusage += ccr->tzlen;
	}

#ifdef HAVE_MMAP
	/* Obtain page size, since mmapped regions must be page-aligned. */
	if ((pagesize = sysconf(_SC_PAGESIZE)) == -1) {
		warnp("sysconf(_SC_PAGESIZE)");
		goto err5;
	}

	/* Map the remainder of the cache into memory. */
	fpos = ftello(R.f);
	if (fpos == -1) {
		warnp("ftello(%s)", R.s);
		goto err5;
	}
	if (fstat(fileno(R.f), &sb)) {
		warnp("fstat(%s)", R.s);
		goto err5;
	}
	if (sb.st_size != fpos + (off_t)R.datalen) {
		warn0("Cache has incorrect size (%jd, expected %jd)\n",
		    (intmax_t)(sb.st_size),
		    (intmax_t)(fpos + (off_t)R.datalen));
		goto err5;
	}
	C->datalen = R.datalen + (size_t)(fpos % pagesize);
	if ((C->data = mmap(NULL, C->datalen, PROT_READ,
#ifdef MAP_NOCORE
	    MAP_PRIVATE | MAP_NOCORE,
#else
	    MAP_PRIVATE,
#endif
	    fileno(R.f), fpos - (fpos % pagesize))) == MAP_FAILED) {
		warnp("mmap(%s)", R.s);
		goto err5;
	}
	R.data = (uint8_t *)C->data + (fpos % pagesize);
#else
	/* Allocate space. */
	C->datalen = R.datalen;
	if (((C->data = malloc(C->datalen)) == NULL) && (C->datalen > 0))
		goto err5;
	if (fread(C->data, C->datalen, 1, R.f) != 1) {
		warnp("fread(%s)", R.s);
		goto err6;
	}
	R.data = (uint8_t *)C->data;
#endif

	/* Iterate through the tree reading chunk headers and trailers. */
	if (patricia_foreach(C->tree, callback_read_data, &R)) {
		warnp("Error reading cache: %s", R.s);
		goto err6;
	}

	/* Free buffer used for storing paths. */
	free(R.sbuf);

	/* Close the cache file. */
	fclose(R.f);

	/* Free string allocated by asprintf. */
	free(R.s);

	/* Success! */
	return (C);

emptycache:
	/* Nothing went wrong, but there's nothing on disk. */
	free(R.s);
	return (C);

err6:
#ifdef HAVE_MMAP
	if (C->datalen > 0)
		munmap(C->data, C->datalen);
#else
	free(C->data);
#endif
err5:
	free(R.sbuf);
	patricia_foreach(C->tree, callback_free, NULL);
err4:
	fclose(R.f);
err3:
	free(R.s);
err2:
	patricia_free(C->tree);
err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * ccache_free(cache):
 * Free the cache and all of its entries.
 */
void
ccache_free(CCACHE * cache)
{
	struct ccache_internal * C = cache;

	if (cache == NULL)
		return;

	/* Free all of the records in the patricia tree. */
	patricia_foreach(C->tree, callback_free, NULL);

	/* Free the patricia tree itself. */
	patricia_free(C->tree);

	/* Unmap memory. */
#ifdef HAVE_MMAP
	if (C->datalen > 0 && munmap(C->data, C->datalen))
		warnp("munmap failed on cache data");
#else
	free(C->data);
#endif

	/* Free the cache. */
	free(C);
}
