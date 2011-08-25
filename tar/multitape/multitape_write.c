#include "bsdtar_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "chunkify.h"
#include "chunks.h"
#include "crypto.h"
#include "dirutil.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape_internal.h"

#include "multitape.h"

/* Mean chunk size desired. */
#define	MEANCHUNK	65536

/*
 * Minimum size of chunk which will be stored as a chunk rather than as a
 * file trailer.  As this value increases up to MEANCHUNK/4, the time spent
 * chunkifying the trailer stream will increase, the total amount of data
 * stored will remain roughly constant, and the number of chunks stored (and
 * thus the per-chunk overhead costs) will decrease.
 */
#define	MINCHUNK	4096

/* Stream parameters. */
struct stream {
	uint8_t * index;	/* Stream chunk index. */
	size_t indexlen;	/* Length of chunk index. */
	size_t indexalloc;	/* Storage allocated for chunk index. */
	CHUNKIFIER * c;		/* Chunkifier for stream. */
};

/*
 * "Cookie" structure created by writetape_open and passed to other functions.
 */
struct multitape_write_internal {
	/* Parameters. */
	char * tapename;	/* Tape name. */
	uint64_t machinenum;	/* Machine number. */
	char * cachedir;	/* Cache directory. */
	time_t ctime;		/* Archive creation time. */
	int argc;		/* Number of command-line arguments. */
	char ** argv;		/* Command-line arguments. */
	int stats_enabled;	/* Stats printed on close. */
	int eof;		/* Tape is truncated at current position. */
	int dryrun;		/* A dry run is being performed. */

	/* Lower level cookies. */
	STORAGE_W * S;		/* Storage layer write cookie. */
	CHUNKS_W * C;		/* Chunk layer write cookie. */
	int lockfd;		/* Lock on cache directory. */
	uint8_t seqnum[32];	/* Transaction sequence number. */

	/* Chunkification state. */
	struct stream h;	/* Header stream. */
	struct stream c;	/* Chunk index stream. */
	struct stream t;	/* Trailer stream. */
	CHUNKIFIER * c_file;	/* Used for chunkifying individual files. */
	off_t c_file_in;	/* Bytes written into c_file. */
	off_t c_file_out;	/* Bytes passed out by c_file. */
	int mode;		/* Tape mode (header, data, end of entry). */

	/* Header buffering. */
	uint8_t * hbuf;		/* Pending archive header. */
	size_t hbufalloc;	/* Length of hbuf memory allocation. */
	size_t hlen;		/* Pending header length. */
	off_t clen;		/* Length of chunkified file data. */
	size_t tlen;		/* Length of file trailer. */

	/* Callbacks to the chunkification cache. */
	void * callback_cookie;
	int (*callback_chunk)(void *, struct chunkheader *);
	int (*callback_trailer)(void *, const uint8_t *, size_t);
};

static int tapepresent(STORAGE_W *, const char *, const char *);
static int store_chunk(uint8_t *, size_t, struct chunkheader *, CHUNKS_W *);
static int handle_chunk(uint8_t *, size_t, struct stream *, CHUNKS_W *);
static chunkify_callback callback_h;
static chunkify_callback callback_t;
static chunkify_callback callback_c;
static chunkify_callback callback_file;
static int endentry(TAPE_W *);
static int flushtape(TAPE_W *, int, int);

/**
 * tapepresent(S, fmt, s):
 * Return 1 if an archive exists with the name sprintf(fmt, s), or 0
 * otherwise.
 */
static int
tapepresent(STORAGE_W * S, const char * fmt, const char * s)
{
	char * tapename;

	/* Generate name. */
	if (asprintf(&tapename, fmt, s) == -1)
		goto err0;

	/* Make sure that there isn't already a tape with this name. */
	switch (multitape_metadata_ispresent(S, tapename)) {
	case 1:
		/* File exists. */
		warn0("An archive already exists with the name \"%s\"",
		    tapename);
		goto eexist;
	case -1:
		/* Something went wrong. */
		goto err1;
	}

	/* Free string allocated by asprintf. */
	free(tapename);

	/* Nothing is in the way. */
	return (0);

eexist:
	free(tapename);

	/* Something is in the way. */
	return (1);

err1:
	free(tapename);
err0:
	/* Failure! */
	return (-1);
}

/**
 * store_chunk(buf, buflen, ch, C):
 * Write the chunk ${buf} of length ${buflen} using the chunk layer cookie
 * ${C}, and populate the chunkheader structure ${ch}.
 */
static int
store_chunk(uint8_t * buf, size_t buflen, struct chunkheader * ch,
    CHUNKS_W * C)
{
	ssize_t zlen;

	/* Hash of chunk. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_CHUNK, buf, buflen, ch->hash))
		goto err0;

	/* Length of chunk. */
	le32enc(ch->len, (uint32_t)(buflen));

	/* Ask chunk layer to store the chunk. */
	zlen = chunks_write_chunk(C, ch->hash, buf, buflen);
	if (zlen == -1) {
		warnp("Error in chunk storage layer");
		goto err0;
	}

	/* Compressed length of chunk. */
	le32enc(ch->zlen, (uint32_t)(zlen));

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * handle_chunk(buf, buflen, S, C):
 * Handle a chunk ${buf} of length ${buflen} belonging to the stream ${S}:
 * Write it using the chunk layer cookie ${C}, and append a chunk header to
 * the stream index.
 */
static int
handle_chunk(uint8_t * buf, size_t buflen, struct stream * S, CHUNKS_W * C)
{
	struct chunkheader ch;
	uint8_t * index_new;
	size_t indexalloc_new;

	if (store_chunk(buf, buflen, &ch, C))
		goto err0;

	/* Enlarge index if needed. */
	while (S->indexalloc - S->indexlen < sizeof(struct chunkheader)) {
		if (S->indexalloc == 0)
			indexalloc_new = sizeof(struct chunkheader);
		else
			indexalloc_new = S->indexalloc * 2;

		/* Handle integer overflows. */
		if (indexalloc_new < S->indexalloc) {
			errno = ENOMEM;
			goto err0;
		}

		index_new = realloc(S->index, indexalloc_new);
		if (index_new == NULL)
			goto err0;

		S->index = index_new;
		S->indexalloc = indexalloc_new;
	}

	/* Copy chunk header into buffer. */
	memcpy(S->index + S->indexlen, &ch, sizeof(struct chunkheader));
	S->indexlen += sizeof(struct chunkheader);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * callback_h(cookie, buf, buflen):
 * Handle a chunk ${buf} of length ${buflen} from the header stream of the
 * tape associated with the multitape write cookie ${cookie}.
 */
static int
callback_h(void * cookie, uint8_t * buf, size_t buflen)
{
	struct multitape_write_internal * d = cookie;

	return (handle_chunk(buf, buflen, &d->h, d->C));
}

/**
 * callback_t(cookie, buf, buflen):
 * Handle a chunk ${buf} of length ${buflen} from the trailer stream of the
 * tape associated with the multitape write cookie ${cookie}.
 */
static int
callback_t(void * cookie, uint8_t * buf, size_t buflen)
{
	struct multitape_write_internal * d = cookie;

	return (handle_chunk(buf, buflen, &d->t, d->C));
}

/**
 * callback_c(cookie, buf, buflen):
 * Handle a chunk ${buf} of length ${buflen} from the chunk index stream of
 * the tape associated with the multitape write cookie ${cookie}.
 */
static int
callback_c(void * cookie, uint8_t * buf, size_t buflen)
{
	struct multitape_write_internal * d = cookie;

	return (handle_chunk(buf, buflen, &d->c, d->C));
}

/**
 * callback_file(cookie, buf, buflen):
 * Handle a chunk ${buf} of length ${buflen} from a file which is being
 * written to the tape associated with the multitape write cookie ${cookie}.
 */
static int
callback_file(void * cookie, uint8_t * buf, size_t buflen)
{
	struct multitape_write_internal * d = cookie;
	struct chunkheader ch;

	/* Data is being passed out by c_file. */
	d->c_file_out += buflen;

	/* Anything under MINCHUNK bytes belongs in the trailer stream. */
	if (buflen < MINCHUNK) {
		/* There shouldn't be any trailer yet. */
		if (d->tlen != 0) {
			warn0("Archive entry has two trailers?");
			goto err0;
		}

		/* Write to the trailer stream. */
		if (chunkify_write(d->t.c, buf, buflen))
			goto err0;

		/* Record the trailer length. */
		d->tlen = buflen;

		/* Call the trailer callback, if one exists. */
		if ((d->callback_trailer != NULL) &&
		    (d->callback_trailer)(d->callback_cookie, buf, buflen))
			goto err0;
	} else {
		/* Store the chunk. */
		if (store_chunk(buf, buflen, &ch, d->C))
			goto err0;

		/* Write chunk header to chunk index stream. */
		if (chunkify_write(d->c.c, (uint8_t *)(&ch),
		    sizeof(struct chunkheader)))
			goto err0;

		/* Record the chunkified data length. */
		d->clen += buflen;

		/* Call the chunk callback, if one exists. */
		if ((d->callback_chunk != NULL) &&
		    (d->callback_chunk)(d->callback_cookie, &ch))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * endentry(d):
 * An archive entry or trailer is ending; flush buffers into the stream.
 */
static int
endentry(TAPE_W * d)
{
	struct entryheader eh;

	/* Construct entry header. */
	le32enc(eh.hlen, d->hlen);
	le64enc(eh.clen, d->clen);
	le32enc(eh.tlen, d->tlen);

	/* Write entry header to header stream. */
	if (chunkify_write(d->h.c, (uint8_t *)(&eh),
	    sizeof(struct entryheader)))
		goto err0;

	/* Write archive header to header stream. */
	if (chunkify_write(d->h.c, d->hbuf, d->hlen))
		goto err0;

	/* Reset pending write lengths. */
	d->hlen = d->clen = d->tlen = 0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * writetape_open(machinenum, cachedir, tapename, argc, argv, printstats,
 *     dryrun):
 * Create a tape with the given name, and return a cookie which can be used
 * for accessing it.  The argument vector must be long-lived.
 */
TAPE_W *
writetape_open(uint64_t machinenum, const char * cachedir,
    const char * tapename, int argc, char ** argv, int printstats,
    int dryrun)
{
	struct multitape_write_internal * d;
	uint8_t lastseq[32];
	size_t argvlen;

	/* Allocate memory. */
	if ((d = malloc(sizeof(struct multitape_write_internal))) == NULL)
		goto err0;
	memset(d, 0, sizeof(struct multitape_write_internal));

	/* Tape starts in "end of entry" mode. */
	d->mode = 2;

	/* Record the machine number. */
	d->machinenum = machinenum;

	/* Copy the tape directory, cache directory, and tape name. */
	if ((d->tapename = strdup(tapename)) == NULL)
		goto err1;
	if ((d->cachedir = strdup(cachedir)) == NULL)
		goto err2;

	/* Record a pointer to the argument vector. */
	d->argv = argv;

	/* Take as many arguments as we can fit into 128 kB. */
	for (argvlen = 0, d->argc = 0; d->argc < argc; d->argc++) {
		argvlen += strlen(argv[d->argc]) + 1;
		if (argvlen > 128000) {
			warn0("Argument vector exceeds 128 kB in length;"
			    " vector stored in archive is being truncated.");
			break;
		}
	}

	/* Record the archive creation time. */
	/*-
	 * XXX POSIX is dumb
	 * XXX Failure is indistinguishable from the valid time (time_t)(-1).
	 * XXX We resolve this by treating the time (time_t)(-1) as invalid.
	 */
	d->ctime = time(NULL);

	/* Record whether we should print archive statistics on close. */
	d->stats_enabled = printstats;

	/* Record whether this is a dry run. */
	d->dryrun = dryrun;

	/* Make sure ${cachedir} exists. */
	if (dirutil_needdir(cachedir))
		goto err3;

	/* Lock the cache directory. */
	if ((d->lockfd = multitape_lock(cachedir)) == -1)
		goto err3;

	/* If this isn't a dry run, finish any pending commit. */
	if ((d->dryrun == 0) && multitape_cleanstate(cachedir, machinenum, 0))
		goto err4;

	/* Get sequence number. */
	if (multitape_sequence(cachedir, lastseq))
		goto err4;

	/* Obtain write cookies from the storage and chunk layers. */
	if ((d->S = storage_write_start(machinenum, lastseq,
	    d->seqnum, d->dryrun)) == NULL)
		goto err4;
	if ((d->C = chunks_write_start(cachedir, d->S, MAXCHUNK,
	    d->dryrun)) == NULL)
		goto err5;

	/*
	 * Make sure that there isn't an archive already present with either
	 * the specified name or that plus ".part" (in case the user decides
	 * to truncate the archive).
	 */
	if ((d->dryrun == 0) && tapepresent(d->S, "%s", tapename))
		goto err6;
	if ((d->dryrun == 0) && tapepresent(d->S, "%s.part", tapename))
		goto err6;

	/* Create chunkifiers. */
	d->h.c = d->c.c = d->t.c = d->c_file = NULL;
	if ((d->h.c = chunkify_init(MEANCHUNK, MAXCHUNK, &callback_h,
	    (void *)d)) == NULL)
		goto err7;
	if ((d->c.c = chunkify_init(MEANCHUNK, MAXCHUNK, &callback_c,
	    (void *)d)) == NULL)
		goto err7;
	if ((d->t.c = chunkify_init(MEANCHUNK, MAXCHUNK, &callback_t,
	    (void *)d)) == NULL)
		goto err7;
	if ((d->c_file = chunkify_init(MEANCHUNK, MAXCHUNK, &callback_file,
	    (void *)d)) == NULL)
		goto err7;

	/* No data has entered or exited c_file. */
	d->c_file_in = d->c_file_out = 0;

	/* Success! */
	return (d);

err7:
	warnp("Error initializing chunkifier");
	chunkify_free(d->c_file);
	chunkify_free(d->t.c);
	chunkify_free(d->c.c);
	chunkify_free(d->h.c);
err6:
	chunks_write_free(d->C);
err5:
	storage_write_free(d->S);
err4:
	close(d->lockfd);
err3:
	free(d->cachedir);
err2:
	free(d->tapename);
err1:
	free(d);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * writetape_setcallbacks(d, callback_chunk, callback_trailer,
 *     callback_cookie):
 * On the tape associated with ${d}, set ${callback_chunk} to be called
 * with the ${callback_cookie} parameter whenever a chunk header is written
 * which corresponds to data provided via writetape_write.  Set
 * ${callback_trailer} to be called whenever a trailer (i.e., file data which
 * is not in a chunk) is written.
 */
void
writetape_setcallback(TAPE_W * d,
    int callback_chunk(void *, struct chunkheader *),
    int callback_trailer(void *, const uint8_t *, size_t),
    void * callback_cookie)
{

	d->callback_chunk = callback_chunk;
	d->callback_trailer = callback_trailer;
	d->callback_cookie = callback_cookie;
}

/**
 * writetape_write(d, buffer, nbytes):
 * Write ${nbytes} bytes of data from ${buffer} to the tape associated with
 * ${d}.  Return ${nbytes} on success.
 */
ssize_t
writetape_write(TAPE_W * d, const void * buffer, size_t nbytes)
{
	uint8_t * hbuf_new;
	size_t hbufalloc_new;

	/* Don't write anything if we're truncating the archive. */
	if (d->eof)
		goto eof;

	switch (d->mode) {
	case 1:
		/* We're in data mode.  Write to the file chunkifier. */
		if (chunkify_write(d->c_file, buffer, nbytes))
			goto err0;
		d->c_file_in += nbytes;
		break;
	case 2:
	case 3:
		/*
		 * We're writing the end-of-archive marker.  No entries
		 * should occur beyond this point.
		 */
		d->mode = 3;
		/* FALLTHROUGH */
	case 0:
		/* We're in header mode.  Append the data to d->hbuf. */

		/* Enlarge the buffer if necessary. */
		while (d->hbufalloc - d->hlen < nbytes) {
			if (d->hbufalloc == 0)
				hbufalloc_new = nbytes;
			else
				hbufalloc_new = d->hbufalloc * 2;

			/* Handle integer overflows. */
			if (hbufalloc_new < d->hbufalloc) {
				errno = ENOMEM;
				goto err0;
			}

			hbuf_new = realloc(d->hbuf, hbufalloc_new);
			if (hbuf_new == NULL)
				goto err0;

			d->hbuf = hbuf_new;
			d->hbufalloc = hbufalloc_new;
		}

		/* Buffer is large enough; copy the data. */
		memcpy(d->hbuf + d->hlen, buffer, nbytes);
		d->hlen += nbytes;
	}

	/* Success! */
	return (nbytes);

eof:
	/* Archive is being truncated; refuse to write anything. */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * writetape_ischunkpresent(d, ch):
 * If the specified chunk exists, return its length; otherwise, return 0.
 */
ssize_t
writetape_ischunkpresent(TAPE_W * d, struct chunkheader * ch)
{

	if (chunks_write_ispresent(d->C, ch->hash) == 0)
		return (le32dec(ch->len));
	else
		return (0);
}

/**
 * writetape_writechunk(d, ch):
 * Attempt to add a (copy of a) pre-existing chunk to the tape being written.
 * Return the length of the chunk if successful; 0 if the chunk cannot be
 * added written via this interface but must instead be written using the
 * writetape_write interface (e.g., if the chunk does not exist or if the
 * tape is not in a state where a chunk can be written); or -1 if an error
 * occurs.
 */
ssize_t
writetape_writechunk(TAPE_W * d, struct chunkheader * ch)
{

	/* Are we in state 1 (archive entry data)? */
	if (d->mode != 1)
		goto notpresent;

	/*
	 * Has all of the data which was written into the file chunkifier
	 * passed through?  (This check is necessary in order to avoid having
	 * file data re-ordered if writetape_writechunk is called after a
	 * call to writetape_write without an intervening mode change).
	 */
	if (d->c_file_in != d->c_file_out)
		goto notpresent;

	/* Attempt to reference the chunk. */
	switch (chunks_write_chunkref(d->C, ch->hash)) {
	case -1:
		goto err0;
	case 1:
		goto notpresent;
	}

	/* Write chunk header to chunk index stream. */
	if (chunkify_write(d->c.c, (uint8_t *)ch,
	    sizeof(struct chunkheader)))
		goto err0;

	/* Adjust "chunkified data length from current entry" value. */
	d->clen += le32dec(ch->len);

	/* Success! */
	return (le32dec(ch->len));

notpresent:
	/* The chunk layer doesn't have a chunk with this hash. */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * writetape_setmode(d, mode):
 * Set the tape mode to 0 (HEADER) or 1 (DATA).
 */
int
writetape_setmode(TAPE_W * d, int mode)
{

	if (mode == d->mode)
		goto done;

	/* If we were in DATA mode, end the current file chunk. */
	if (d->mode == 1) {
		if (chunkify_end(d->c_file))
			goto err0;
	}

	/* If we have written an archive trailer, we can't change the mode. */
	if (d->mode == 3) {
		warn0("Programmer error: "
		    "Archive entry occurs after archive trailer.");
		goto err0;
	}

	/* If the entry is ending, write to the header stream. */
	if (mode == 2) {
		if (endentry(d))
			goto err0;
	}

	/* Record the new mode. */
	d->mode = mode;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * writetape_truncate(d):
 * Record that the archive is being truncated at the current position.
 */
void
writetape_truncate(TAPE_W * d)
{

	d->eof = 1;
}

/**
 * flushtape(d, isapart, extrastats):
 * Flush the chunkifiers, store metaindex and metadata, and issue a flush at
 * the storage layer.  If ${extrastats} is non-zero, add the metaindex and
 * metadata sizes to storage statistics.
 */
static int
flushtape(TAPE_W * d, int isapart, int extrastats)
{
	struct tapemetadata tmd;
	struct tapemetaindex tmi;
	char * tapename;

	/* Tell the chunkifiers that there will be no more data. */
	if (chunkify_end(d->c_file))
		goto err0;
	if (chunkify_end(d->t.c))
		goto err0;
	if (chunkify_end(d->c.c))
		goto err0;
	if (chunkify_end(d->h.c))
		goto err0;

	/* Construct tape name. */
	if (isapart) {
		if (asprintf(&tapename, "%s.part", d->tapename) == -1)
			goto err0;
	} else {
		if (asprintf(&tapename, "%s", d->tapename) == -1)
			goto err0;
	}

	/* Fill in archive metadata & metaindex structures. */
	tmd.name = tapename;
	tmd.ctime = d->ctime;
	tmd.argc = d->argc;
	tmd.argv = d->argv;
	tmi.hindex = d->h.index;
	tmi.hindexlen = d->h.indexlen;
	tmi.cindex = d->c.index;
	tmi.cindexlen = d->c.indexlen;
	tmi.tindex = d->t.index;
	tmi.tindexlen = d->t.indexlen;

	/*
	 * Store archive metaindex.  Note that this must be done before the
	 * archive metadata is stored, since it fills in fields in the archive
	 * metadata concerning the index length and hash.
	 */
	if (multitape_metaindex_put(d->S, d->C, &tmi, &tmd, extrastats))
		goto err1;

	/* Store archive metadata. */
	if (multitape_metadata_put(d->S, d->C, &tmd, extrastats))
		goto err1;

	/* Free string allocated by asprintf. */
	free(tapename);

	/* Ask the storage layer to flush all pending writes. */
	if (storage_write_flush(d->S))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(tapename);
err0:
	/* Failure! */
	return (-1);
}

/**
 * writetape_checkpoint(d):
 * Create a checkpoint in the tape associated with ${d}.
 */
int
writetape_checkpoint(TAPE_W * d)
{
	int mode_saved;

	/*
	 * If we're in the middle of an archive entry, we need to switch to
	 * mode 2 (end of archive entry) so that data gets flushed through,
	 * and then switch back to the original mode later (which may result
	 * in an archive "entry" with no header data -- this is fine).
	 */
	mode_saved = d->mode;
	if (mode_saved < 2) {
		if (writetape_setmode(d, 2))
			goto err0;
	}

	/*
	 * Deal with any archive trailer, in the unlikely case that we're
	 * being asked to create a checkpoint when the archive is about to
	 * be closed.
	 */
	if ((d->mode == 3) && endentry(d))
		goto err0;

	/*
	 * Flush data through and write the metaindex and metadata;
	 * checkpoints are partial archives, so mark it as such.
	 */
	if (flushtape(d, 1, 0))
		goto err0;

	/* Ask the chunks layer to prepare for a checkpoint. */
	if (chunks_write_checkpoint(d->C))
		goto err0;

	/* If this isn't a dry run, create a checkpoint. */
	if ((d->dryrun == 0) &&
	    multitape_checkpoint(d->cachedir, d->machinenum, d->seqnum))
		goto err0;

	/* If we changed the tape mode, switch back to the original mode. */
	if (mode_saved < 2) {
		if (writetape_setmode(d, mode_saved))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * writetape_close(d):
 * Close the tape associated with ${d}.
 */
int
writetape_close(TAPE_W * d)
{

	/* If the archive is truncated, end any current archive entry. */
	if (d->eof && (d->mode < 2) && writetape_setmode(d, 2))
		goto err2;

	/* If a file trailer was written, deal with it. */
	if ((d->mode == 3) && endentry(d))
		goto err2;

	/*
	 * Make sure we're not being called in the middle of an archive
	 * entry unless we're truncating an archive.
	 */
	if ((d->mode < 2) && (d->eof == 0)) {
		/* We shouldn't be in the middle of an archive entry. */
		warn0("Programmer error: writetape_close called in mode %d",
		    d->mode);
		goto err2;
	}

	/* Flush data through and write the metaindex and metadata. */
	if (flushtape(d, d->eof, 1))
		goto err2;

	/* Print statistics, if we've been asked to do so. */
	if (d->stats_enabled && chunks_write_printstats(stderr, d->C))
		goto err2;

	/* Ask the chunks layer to prepare for a checkpoint. */
	if (chunks_write_checkpoint(d->C))
		goto err2;

	/* Close the chunk layer and storage layer cookies. */
	chunks_write_free(d->C);
	if (storage_write_end(d->S))
		goto err1;

	/*
	 * If this isn't a dry run, create a checkpoint and commit the
	 * write transaction.
	 */
	if (d->dryrun == 0) {
		if (multitape_checkpoint(d->cachedir, d->machinenum,
		    d->seqnum))
			goto err1;
		if (multitape_cleanstate(d->cachedir, d->machinenum, 0))
			goto err1;
	}

	/* Unlock the cache directory. */
	close(d->lockfd);

	/* Free memory. */
	chunkify_free(d->c_file);
	chunkify_free(d->t.c);
	chunkify_free(d->c.c);
	chunkify_free(d->h.c);
	free(d->cachedir);
	free(d->tapename);
	free(d);

	/* Success! */
	return (0);

err2:
	chunks_write_free(d->C);
	storage_write_free(d->S);
err1:
	close(d->lockfd);
	chunkify_free(d->c_file);
	chunkify_free(d->t.c);
	chunkify_free(d->c.c);
	chunkify_free(d->h.c);
	free(d->cachedir);
	free(d->tapename);
	free(d);

	/* Failure! */
	return (-1);
}

/**
 * writetape_free(d):
 * Free memory associated with ${d}; the archive is being cancelled.
 */
void
writetape_free(TAPE_W * d)
{

	chunks_write_free(d->C);
	storage_write_free(d->S);
	close(d->lockfd);
	chunkify_free(d->c_file);
	chunkify_free(d->t.c);
	chunkify_free(d->c.c);
	chunkify_free(d->h.c);
	free(d->cachedir);
	free(d->tapename);
	free(d);
}
