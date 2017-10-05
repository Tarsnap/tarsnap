#include "bsdtar_platform.h"

#include <sys/types.h>

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chunks.h"
#include "crypto.h"
#include "ctassert.h"
#include "multitape_internal.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape.h"

/*
 * The readtape_read API relies upon chunks being no more than SSIZE_MAX
 * bytes in length.
 */
CTASSERT(MAXCHUNK <= SSIZE_MAX);

/* Stream parameters. */
struct stream {
	struct stream * istr;	/* Index stream. */
	uint8_t * chunk;	/* Buffer for holding a chunk. */
	size_t chunklen;	/* Length of current chunk. */
	size_t chunkpos;	/* Position within current chunk. */
	off_t skiplen;		/* Length to skip. */
	struct chunkheader ch;	/* Pending chunk header. */
	int ch_valid;		/* Non-zero if ch is valid. */
};

/* Cookie created by readtape_open and passed to other functions. */
struct multitape_read_internal {
	struct stream h;	/* Headers. */
	struct stream hi;	/* Headers index. */
	struct stream c;	/* Chunks. */
	struct stream ci;	/* Chunks index. */
	struct stream cii;	/* Chunks index index. */
	struct stream t;	/* Trailers. */
	struct stream ti;	/* Trailers index. */
	off_t hlen;		/* Queued length of header. */
	off_t clen;		/* Queued length of chunked data. */
	off_t tlen;		/* Queued length of trailer. */
	struct tapemetaindex tmi;	/* Metaindex. */
	STORAGE_R * S;		/* Storage layer cookie. */
	CHUNKS_R * C;		/* Chunk layer cookie. */
};

static int stream_get_chunkheader(struct stream *, CHUNKS_R *);
static int stream_get_chunk(struct stream *, const uint8_t **, size_t *,
    CHUNKS_R *);
static ssize_t stream_read(struct stream *, uint8_t *, size_t, CHUNKS_R *);

/**
 * stream_get_chunkheader(S, C):
 * Fill ${S}->ch with the header for the next chunk, and set ch_valid to 1.
 * On EOF of the parent stream, ch_valid will remain zero.
 */
static int
stream_get_chunkheader(struct stream * S, CHUNKS_R * C)
{
	off_t len;
	ssize_t readlen;

	/* Loop until we have a chunk which we're not skipping. */
	do {
		if (S->ch_valid) {
			len = le32dec(S->ch.len);
			if (len <= S->skiplen) {
				S->skiplen -= len;
				S->ch_valid = 0;
			} else {
				/* We have a useful chunk. */
				break;
			}
		}

		/* Get a chunk header from the parent stream. */
		readlen = stream_read(S->istr, (uint8_t *)&S->ch,
		    sizeof(struct chunkheader), C);

		switch (readlen) {
		case -1:
			/* Error in stream_read. */
			goto err0;
		case 0:
			/* No more chunks available. */
			goto eof;
		case sizeof(struct chunkheader):
			/* Successful read of chunk header. */
			S->ch_valid = 1;
			break;
		default:
			/* Wrong length read. */
			warnp("Premature EOF of archive index");
			goto err0;
		}
	} while (1);

eof:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * stream_get_chunk(S, buf, clen, C):
 * Set ${buf} to point to the next available data, and set ${clen} to the
 * length of data available; use the chunk read cookie ${C} if needed.
 */
static int
stream_get_chunk(struct stream * S, const uint8_t ** buf, size_t * clen,
    CHUNKS_R * C)
{
	size_t len, zlen;
	off_t skip;

	/* Skip part of the current chunk if appropriate. */
	if (S->skiplen) {
		skip = (off_t)(S->chunklen - S->chunkpos);
		if (skip > S->skiplen)
			skip = S->skiplen;

		S->skiplen -= skip;
		S->chunkpos += (size_t)skip;
	}

	while ((S->chunklen == S->chunkpos) && (S->istr != NULL)) {
		/* Get a chunk header. */
		if (stream_get_chunkheader(S, C))
			goto err0;

		/* EOF? */
		if (S->ch_valid == 0)
			goto eof;

		/* Decode chunk header. */
		len = le32dec(S->ch.len);
		zlen = le32dec(S->ch.zlen);

		/* Read chunk. */
		if (chunks_read_chunk(C, S->ch.hash, len, zlen, S->chunk, 0))
			goto err0;
		S->chunklen = len;

		/* Set current position within buffer. */
		S->chunkpos = (size_t)S->skiplen;
		S->skiplen = 0;

		/* The chunk is no longer pending. */
		S->ch_valid = 0;
	}

	/* We have some data. */
	*buf = S->chunk + S->chunkpos;
	*clen = S->chunklen - S->chunkpos;

	/* Success! */
	return (0);

eof:
	*clen = 0;
	*buf = NULL;

	/* Success, but no data. */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * stream_read(S, buf, buflen, C):
 * Fill ${buf} with ${buflen} bytes of data from the stream ${S}.  Return the
 * length written (which may be less than ${buflen} on EOF).
 */
static ssize_t
stream_read(struct stream * S, uint8_t * buf, size_t buflen, CHUNKS_R * C)
{
	const uint8_t * readbuf;
	size_t readlen;
	size_t bufpos;

	/* Sanity check. */
	assert(buflen < SSIZE_MAX);

	for (bufpos = 0; bufpos < buflen; bufpos += readlen) {
		/* Read data. */
		if (stream_get_chunk(S, &readbuf, &readlen, C))
			goto err0;

		/* Make sure we don't have too much data. */
		if (readlen > buflen - bufpos)
			readlen = buflen - bufpos;

		/* Stop looping if we have no more data. */
		if (readlen == 0)
			break;

		/* Mark the data as consumed. */
		S->chunkpos += readlen;

		/* Copy into the correct position in our buffer. */
		memcpy(buf + bufpos, readbuf, readlen);
	}

	/* Success (or perhaps EOF). */
	return ((ssize_t)bufpos);

err0:
	/* Failure! */
	return (-1);
}

/**
 * get_entryheader(d):
 * Read an archive entry header and update the pending header, chunk and
 * trailer data lengths.  Return -1 on error, 0 on EOF, or 1 on success.
 */
static int
get_entryheader(TAPE_R * d)
{
	struct entryheader eh;
	ssize_t readlen;

	/* Read an archive entry header. */
	readlen = stream_read(&d->h, (uint8_t *)&eh,
	    sizeof(struct entryheader), d->C);

	switch (readlen) {
	case -1:
		/* Error in stream_read. */
		return (-1);
	case 0:
		/* No more chunks available. */
		return (0);
	case sizeof(struct entryheader):
		/* Successful read of chunk header.  Decode entry header. */
		d->hlen = le32dec(eh.hlen);
		d->clen = (off_t)le64dec(eh.clen);
		d->tlen = le32dec(eh.tlen);
		return (1);
	default:
		/* Wrong length read. */
		warnp("Premature EOF of archive index");
		return (-1);
	}
}

/**
 * readtape_open(machinenum, tapename):
 * Open the tape with the given name, and return a cookie which can be used
 * for accessing it.
 */
TAPE_R *
readtape_open(uint64_t machinenum, const char * tapename)
{
	struct multitape_read_internal * d;
	struct tapemetadata tmd;

	/* Allocate memory. */
	if ((d = malloc(sizeof(struct multitape_read_internal))) == NULL)
		goto err0;
	memset(d, 0, sizeof(struct multitape_read_internal));

	/* Obtain a storage layer read cookie. */
	if ((d->S = storage_read_init(machinenum)) == NULL)
		goto err1;

	/* Obtain a chunk layer read cookie. */
	if ((d->C = chunks_read_init(d->S, MAXCHUNK)) == NULL)
		goto err2;

	/* Allocate chunk buffers. */
	d->h.chunk = malloc(MAXCHUNK);
	d->c.chunk = malloc(MAXCHUNK);
	d->ci.chunk = malloc(MAXCHUNK);
	d->t.chunk = malloc(MAXCHUNK);
	if ((d->h.chunk == NULL) || (d->c.chunk == NULL) ||
	    (d->ci.chunk == NULL) || (d->t.chunk == NULL))
		goto err3;

	/* Initialize streams. */
	d->h.istr = &d->hi;
	d->c.istr = &d->ci;
	d->ci.istr = &d->cii;
	d->t.istr = &d->ti;

	/* Read the tape metadata. */
	if (multitape_metadata_get_byname(d->S, NULL, &tmd, tapename, 0))
		goto err3;

	/* Read the tape metaindex. */
	if (multitape_metaindex_get(d->S, NULL, &d->tmi, &tmd, 0))
		goto err4;

	/* Free parsed metadata. */
	multitape_metadata_free(&tmd);

	/* Initialize streams. */
	d->hi.chunklen = d->tmi.hindexlen;
	d->hi.chunk = d->tmi.hindex;
	d->cii.chunklen = d->tmi.cindexlen;
	d->cii.chunk = d->tmi.cindex;
	d->ti.chunklen = d->tmi.tindexlen;
	d->ti.chunk = d->tmi.tindex;

	/* Success! */
	return (d);

err4:
	multitape_metadata_free(&tmd);
err3:
	free(d->h.chunk);
	free(d->c.chunk);
	free(d->ci.chunk);
	free(d->t.chunk);
	chunks_read_free(d->C);
err2:
	storage_read_free(d->S);
err1:
	free(d);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * readtape_read(d, buffer):
 * Read some data from the tape associated with ${d}, make ${*buffer}
 * point to the data, and return the number of bytes read.
 */
ssize_t
readtape_read(TAPE_R * d, const void ** buffer)
{
	const uint8_t ** buf = (const uint8_t **)buffer;
	struct stream * readstream;
	off_t * readmaxlen;
	size_t clen;

	/* Loop until we read EOF or have some data to return. */
	do {
		if (d->hlen) {
			/* We want some header data. */
			readstream = &d->h;
			readmaxlen = &d->hlen;
		} else if (d->clen) {
			/* We want some chunk data. */
			readstream = &d->c;
			readmaxlen = &d->clen;
		} else if (d->tlen) {
			/* We want some trailer data. */
			readstream = &d->t;
			readmaxlen = &d->tlen;
		} else {
			/* Read the next archive entry header. */
			switch (get_entryheader(d)) {
			case -1:
				goto err0;
			case 0:
				goto eof;
			case 1:
				break;
			}

			continue;
		}

		if (stream_get_chunk(readstream, buf, &clen, d->C))
			goto err0;
		if ((off_t)clen > *readmaxlen)
			clen = (size_t)(*readmaxlen);

		readstream->chunkpos += clen;
		*readmaxlen -= clen;

		/* Do we have data? */
		if (clen)
			break;

		/* Premature EOF. */
		warnp("Premature EOF reading archive");
		goto err0;
	} while (1);

	/* Sanity check. */
	if (clen > SSIZE_MAX) {
		warn0("Chunk is too large");
		goto err0;
	}

	/* Success! */
	return ((ssize_t)clen);

eof:
	/* No more data. */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * readtape_readchunk(d, ch):
 * Obtain a chunk header suitable for passing to writetape_writechunk.
 * Return the length of the chunk, or 0 if no chunk is available (EOF or if
 * the tape position isn't aligned at a chunk boundary).
 */
ssize_t
readtape_readchunk(TAPE_R * d, struct chunkheader ** ch)
{
	size_t len;

	/*
	 * If we've hit the end of a multitape archive entry, read the next
	 * entry header.  If a single file is split between two or more
	 * multitape entries due to checkpointing, the second and subsequent
	 * entries will have no header data but will instead go straight into
	 * chunks.
	 */
	if ((d->hlen == 0) && (d->clen == 0) && (d->tlen == 0)) {
		/* Read the next archive entry header. */
		switch (get_entryheader(d)) {
		case -1:
			goto err0;
		case 0:
			goto nochunk;
		case 1:
			break;
		}
	}

	/*
	 * We can only return a chunk if we're in the chunk portion of an
	 * archive entry.
	 */
	if (d->hlen != 0 || d->clen == 0)
		goto nochunk;

	/*
	 * Make sure we're not in the middle of a chunk (this should never
	 * happen, since this stream contains complete chunks from files!)
	 */
	if (d->c.chunkpos != d->c.chunklen) {
		warn0("c.chunkpos != c.chunklen");
		goto err0;
	}

	/* Get a chunk header. */
	if (stream_get_chunkheader(&d->c, d->C))
		goto err0;

	/*
	 * EOF is an error, but we'll ignore it and let it be reported when
	 * readtape_read is next called.
	 */
	if (d->c.ch_valid == 0)
		goto nochunk;

	/* We need to be properly aligned on a chunk boundary. */
	if (d->c.skiplen != 0)
		goto nochunk;

	/* We have a chunk! */
	*ch = &d->c.ch;
	len = le32dec(d->c.ch.len);

	/* Sanity check. */
	if (len > SSIZE_MAX) {
		warn0("Chunk is too large");
		goto err0;
	}

	/* Return the chunk length. */
	return ((ssize_t)len);

nochunk:
	/* We don't have a chunk available. */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * readtape_skip(d, request):
 * Skip up to ${request} bytes from the tape associated with ${d},
 * and return the length skipped.
 */
off_t
readtape_skip(TAPE_R * d, off_t request)
{
	off_t skipped;
	off_t skiplen;

	/* Loop until we have skipped enough. */
	for (skipped = 0; skipped < request;) {
		if (d->hlen) {
			/* We want to skip some header data. */
			if (request - skipped < d->hlen)
				skiplen = request - skipped;
			else
				skiplen = d->hlen;
			d->hlen -= skiplen;
			d->h.skiplen += skiplen;
			skipped += skiplen;
		} else if (d->clen) {
			/* We want to skip some chunk data. */
			if (request - skipped < d->clen)
				skiplen = request - skipped;
			else
				skiplen = d->clen;
			d->clen -= skiplen;
			d->c.skiplen += skiplen;
			skipped += skiplen;
		} else if (d->tlen) {
			/* We want to skip some trailer data. */
			if (request - skipped < d->tlen)
				skiplen = request - skipped;
			else
				skiplen = d->tlen;
			d->tlen -= skiplen;
			d->t.skiplen += skiplen;
			skipped += skiplen;
		} else {
			/* Read the next archive entry header. */
			switch (get_entryheader(d)) {
			case -1:
				goto err0;
			case 0:
				goto eof;
			case 1:
				break;
			}
		}
	}

	/* Success! */
	return (skipped);

eof:
	/* No more data. */
	return (skipped);

err0:
	/* Failure! */
	return (-1);
}

/**
 * readtape_close(d):
 * Close the tape associated with ${d}.
 */
int
readtape_close(TAPE_R * d)
{

	/* Free metaindex buffers. */
	multitape_metaindex_free(&d->tmi);

	/* Free buffers. */
	free(d->h.chunk);
	free(d->c.chunk);
	free(d->ci.chunk);
	free(d->t.chunk);

	/* Close the chunk layer read cookie. */
	chunks_read_free(d->C);

	/* Close the storage layer read cookie. */
	storage_read_free(d->S);

	/* Free the multitape layer read cookie. */
	free(d);

	/* Success! */
	return (0);
}
