#include "bsdtar_platform.h"

#include <stdlib.h>
#include <string.h>

#include "chunks.h"
#include "crypto.h"
#include "ctassert.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape_internal.h"

/*
 * The buffer management logic requires that a struct chunkdata and a
 * maximum-size chunk fit into a buffer.
 */
CTASSERT(MAXCHUNK < SIZE_MAX - sizeof(struct chunkheader));

/**
 * multitape_chunkiter_tmd(S, C, tmd, func, cookie, quiet):
 * Call ${func} on ${cookie} and each struct chunkheader involved in the
 * archive associated with the metadata ${tmd}.  If ${C} is non-NULL, call
 * chunks_stats_extrastats on ${C} and the length of each metadata fragment.
 * If ${quiet}, don't print any warnings about corrupt or missing files.
 * Return 0 (success), 1 (a required file is missing), 2 (a required file is
 * corrupt), -1 (error), or the first non-zero value returned by ${func}.
 */
int
multitape_chunkiter_tmd(STORAGE_R * S, CHUNKS_S * C,
    const struct tapemetadata * tmd,
    int func(void *, struct chunkheader *), void * cookie, int quiet)
{
	CHUNKS_R * CR;		/* Chunk layer read cookie. */
	struct tapemetaindex tmi;	/* Metaindex. */
	size_t hindexpos;	/* Header stream index position. */
	size_t cindexpos;	/* Chunk index stream index position. */
	size_t tindexpos;	/* Trailer stream index position. */
	uint8_t * ibuf;		/* Contains a tape index chunk. */
	size_t ibufpos;		/* Position within ibuf. */
	size_t ibuflen;		/* Length of valid data in ibuf. */
	struct chunkheader * ch;	/* Chunk header being processed. */
	size_t chunklen, chunkzlen;	/* Decoded chunk parameters. */
	int rc;

	/* Obtain a chunk layer read cookie. */
	if ((CR = chunks_read_init(S, MAXCHUNK)) == NULL) {
		rc = -1;
		goto err0;
	}

	/* Read the tape metaindex. */
	if ((rc = multitape_metaindex_get(S, C, &tmi, tmd, quiet)) != 0)
		goto err1;

	/* Allocate a buffer for holding chunks of index. */
	if ((ibuf = malloc(MAXCHUNK + sizeof(struct chunkheader))) == NULL) {
		rc = -1;
		goto err2;
	}
	ibuflen = 0;

	/* Iterate through the header stream index. */
	for (hindexpos = 0;
	    hindexpos + sizeof(struct chunkheader) <= tmi.hindexlen;
	    hindexpos += sizeof(struct chunkheader)) {
		ch = (struct chunkheader *)(&tmi.hindex[hindexpos]);
		if ((rc = func(cookie, ch)) != 0)
			goto err3;
	}

	/* Iterate through the chunk index stream index. */
	for (cindexpos = 0;
	    cindexpos + sizeof(struct chunkheader) <= tmi.cindexlen;
	    cindexpos += sizeof(struct chunkheader)) {
		/* Call func on the next chunk from the stream. */
		ch = (struct chunkheader *)(&tmi.cindex[cindexpos]);
		if ((rc = func(cookie, ch)) != 0)
			goto err3;

		/* Decode chunk header. */
		chunklen = le32dec(ch->len);
		chunkzlen = le32dec(ch->zlen);

		/* Sanity check. */
		if (chunklen > MAXCHUNK) {
			if (quiet == 0)
				warn0("Chunk exceeds maximum size");
			rc = 2;
			goto err3;
		}

		/* We want to cache this chunk after reading it. */
		if (chunks_read_cache(CR, ch->hash))
			goto err2;

		/* Read the chunk into buffer. */
		if ((rc = chunks_read_chunk(CR, ch->hash, chunklen, chunkzlen,
		    ibuf + ibuflen, quiet)) != 0)
			goto err3;
		ibuflen += chunklen;

		/* Handle any chunk headers within ibuf. */
		for (ibufpos = 0;
		    ibufpos + sizeof(struct chunkheader) <= ibuflen;
		    ibufpos += sizeof(struct chunkheader)) {
			/* Deal with a chunk header. */
			ch = (struct chunkheader *)(&ibuf[ibufpos]);
			if ((rc = func(cookie, ch)) != 0)
				goto err3;
		}

		/* Move buffered data to the start of the buffer. */
		memmove(ibuf, ibuf + ibufpos, ibuflen - ibufpos);
		ibuflen -= ibufpos;
	}

	/* Iterate through the trailer stream index. */
	for (tindexpos = 0;
	    tindexpos + sizeof(struct chunkheader) <= tmi.tindexlen;
	    tindexpos += sizeof(struct chunkheader)) {
		ch = (struct chunkheader *)(&tmi.tindex[tindexpos]);
		if ((rc = func(cookie, ch)) != 0)
			goto err3;
	}

	/* Free index chunk buffer. */
	free(ibuf);

	/* Free metaindex buffers. */
	multitape_metaindex_free(&tmi);

	/* Close handles. */
	chunks_read_free(CR);

	/* Success! */
	return (0);

err3:
	free(ibuf);
err2:
	multitape_metaindex_free(&tmi);
err1:
	chunks_read_free(CR);
err0:
	/* Failure! */
	return (rc);
}
