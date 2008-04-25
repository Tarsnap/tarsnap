#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "ccache_internal.h"
#include "crypto.h"
#include "multitape.h"
#include "multitape_internal.h"
#include "network.h"
#include "patricia.h"
#include "sysendian.h"
#include "warnp.h"

#include "ccache.h"

static int callback_addchunk(void * cookie, struct chunkheader * ch);
static int callback_addtrailer(void * cookie, const uint8_t * buf, size_t buflen);

/* Callback to add a chunk header to a cache entry. */
static int
callback_addchunk(void * cookie, struct chunkheader * ch)
{
	struct ccache_record * ccr = cookie;
	struct chunkheader * p;
	size_t nchalloc_new;

	/* Do we need to expand the allocate space? */
	if (ccr->nch >= ccr->nchalloc) {
		/* Double the allocated memory. */
		if (ccr->nchalloc)
			nchalloc_new = ccr->nchalloc * 2;
		else {
			/* No data or mmapped data. */
			nchalloc_new = ccr->nch + 1;
		}

		/* Make sure we don't overflow. */
		if (nchalloc_new > 
		    SIZE_MAX / sizeof(struct chunkheader)) {
			errno = ENOMEM;
			goto err0;
		}

		/* Attempt to reallocate. */
		if (ccr->nchalloc) {
			if ((p = realloc(ccr->chp, nchalloc_new *
			    sizeof(struct chunkheader))) == NULL)
				goto err0;
		} else {
			if ((p = malloc(nchalloc_new *
			    sizeof(struct chunkheader))) == NULL)
				goto err0;
			memcpy(p, ccr->chp, ccr->nch *
			    sizeof(struct chunkheader));
		}

		/* Successfully reallocated. */
		ccr->chp = p;
		ccr->nchalloc = nchalloc_new;
	}

	/* We now have space; add the new record. */
	memcpy(ccr->chp + ccr->nch, ch, sizeof(struct chunkheader));
	ccr->nch += 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback to add a file trailer. */
static int
callback_addtrailer(void * cookie, const uint8_t * buf, size_t buflen)
{
	struct ccache_record * ccr = cookie;
	uint8_t * zbuf;
	uLongf zlen;
	int rc;

	/* Do we already have a trailer?  We shouldn't. */
	if (ccr->tlen != 0) {
		warn0("cache entry has two trailers?");
		goto err0;
	}

	/* Allocate space for the trailer. */
	zlen = buflen + (buflen >> 9) + 13;
	if ((zbuf = malloc(zlen)) == NULL)
		goto err0;

	/* Compress trailer. */
	if ((rc = compress2(zbuf, &zlen, buf, buflen, 9)) != Z_OK) {
		warn0("zlib error in compress2: %d", rc);
		goto err1;
	}

	/* Reallocate to correct length. */
	if ((ccr->ztrailer = realloc(zbuf, zlen)) == NULL)
		goto err1;
	ccr->tlen = buflen;
	ccr->tzlen = zlen;
	ccr->flags = ccr->flags | CCR_ZTRAILER_MALLOC;

	/* Success! */
	return (0);

err1:
	free(zbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * ccache_entry_lookup(cache, path, sb, cookie, fullentry):
 * An archive entry is being written for the file ${path} with lstat data
 * ${sb}, to the multitape with write cookie ${cookie}.  Look up the file in
 * the chunkification cache ${cache}, and set ${fullentry} to a non-zero
 * value iff the cache can provide at least sb->st_size bytes of the archive
 * entry.  Return a cookie which can be passed to either ccahe_entry_write
 * or ccache_entry_start depending upon whether ${fullentry} is zero or not.
 */
CCACHE_ENTRY *
ccache_entry_lookup(CCACHE * cache, const char * path, const struct stat * sb,
    TAPE_W * cookie, int * fullentry)
{
	struct ccache_internal * C = cache;
	struct ccache_entry * cce;
	size_t cnum;
	ssize_t lenwrit;
	uLongf tbuflen;
	int rc;
	off_t skiplen;

	/* Allocate memory. */
	if ((cce = malloc(sizeof(struct ccache_entry))) == NULL)
		goto err0;

	/* Record the new inode number, size, and modification time. */
	cce->ino_new = sb->st_ino;
	cce->size_new = sb->st_size;
	cce->mtime_new = sb->st_mtime;

	/* Look up cache entry. */
	if ((cce->ccrp = (struct ccache_record **)patricia_lookup(C->tree,
	    (const uint8_t *)path, strlen(path))) == NULL) {
		/* No cache entry for this path.  Create an empty record. */
		if ((cce->ccr = malloc(sizeof(struct ccache_record))) == NULL)
			goto err1;
		memset(cce->ccr, 0, sizeof(struct ccache_record));

		/* No decompressed trailer. */
		cce->trailer = NULL;

		/* We can't supply the full archive entry. */
		*fullentry = 0;

		/* That's all, folks! */
		goto done;
	}

	/* Entry is in the tree. */
	cce->ccr = *cce->ccrp;

	/* Is the cache entry fresh? */
	if ((cce->ino_new == cce->ccr->ino) &&
	    (cce->size_new == cce->ccr->size) &&
	    (cce->mtime_new == cce->ccr->mtime)) {
		/* Can't provide any data yet. */
		skiplen = 0;

		/* Check if chunks are still available. */
		for (cnum = 0; cnum < cce->ccr->nch; cnum++) {
			lenwrit = writetape_ischunkpresent(cookie,
			    cce->ccr->chp + cnum);

			/* Error? */
			if (lenwrit < 0)
				goto err1;

			/* Not present? */
			if (lenwrit == 0) {
				/* Remove stale data from the cache entry. */
				cce->ccr->nch = cnum;
				if (cce->ccr->flags & CCR_ZTRAILER_MALLOC)
					free(cce->ccr->ztrailer);
				cce->ccr->ztrailer = NULL;
				cce->ccr->tlen = cce->ccr->tzlen = 0;
				break;
			}

			/* We can supply this data. */
			skiplen += lenwrit;
		}

		/*
		 * If all the chunks are available and the cache entry
		 * contains a file trailer, decompress it.
		 */
		if (cce->ccr->tlen > 0 && cnum == cce->ccr->nch) {
			/* Allocate space for trailer. */
			tbuflen = cce->ccr->tlen;
			if ((cce->trailer = malloc(tbuflen)) == NULL)
				goto err1;

			/* Decompress trailer. */
			rc = uncompress(cce->trailer, &tbuflen,
			    cce->ccr->ztrailer, cce->ccr->tzlen);

			/* Print warnings. */
			if (rc != Z_OK)
				warn0("Error decompressing cached trailer: "
				    "zlib error %d", rc);
			else if (tbuflen != cce->ccr->tlen)
				warn0("Cached trailer is corrupt");

			/*
			 * Add the trailer size to the length of the data
			 * which we can supply, unless something went wrong
			 * with the trailer; in which case, free it.
			 */
			if (rc == Z_OK && tbuflen == cce->ccr->tlen) {
				skiplen += cce->ccr->tlen;
			} else {
				free(cce->trailer);
				cce->trailer = NULL;
				if (cce->ccr->flags & CCR_ZTRAILER_MALLOC)
					free(cce->ccr->ztrailer);
				cce->ccr->ztrailer = NULL;
				cce->ccr->tlen = cce->ccr->tzlen = 0;
			}
		} else {
			cce->trailer = NULL;
		}

		/*
		 * Can we supply all the necessary data?  Note that if the
		 * cached archive entry is shorter than the file (e.g., if
		 * it was previously stored as a hardlink), we might find
		 * that everything in the cache is fine, but we still don't
		 * have all the file data.
		 */
		if (skiplen >= sb->st_size)
			*fullentry = 1;
		else
			*fullentry = 0;
	} else {
		/* Cache entry is stale; we can't supply the entire file. */
		*fullentry = 0;

		/* The trailer is useless, so we might as well free it now. */
		if (cce->ccr->flags & CCR_ZTRAILER_MALLOC)
			free(cce->ccr->ztrailer);
		cce->ccr->ztrailer = NULL;
		cce->ccr->tlen = cce->ccr->tzlen = 0;

		/* There is no decompressed trailer, either. */
		cce->trailer = NULL;
	}

done:
	/* Success! */
	return (cce);

err1:
	free(cce);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * ccache_entry_write(cce, cookie):
 * Write the cached archive entry ${cce} to the multitape with write cookie
 * ${cookie}.  Note that this may only be called if ${cce} was returned by
 * a ccache_entry_lookup which set ${fullentry} to a non-zero value.  Return
 * the length written.
 */
off_t
ccache_entry_write(CCACHE_ENTRY * cce, TAPE_W * cookie)
{
	off_t skiplen = 0;
	size_t cnum;
	ssize_t lenwrit;

	/* Make sure the cache entry isn't stale. */
	if ((cce->ino_new != cce->ccr->ino) ||
	    (cce->size_new != cce->ccr->size) ||
	    (cce->mtime_new != cce->ccr->mtime)) {
		warn0("Programmer error: "
		    "ccache_entry_write called with stale cache entry");
		goto err0;
	}

	/* Write chunks. */
	for (cnum = 0; cnum < cce->ccr->nch; cnum++) {
		lenwrit = writetape_writechunk(cookie, cce->ccr->chp + cnum);

		/* Error? */
		if (lenwrit < 0)
			goto err0;

		/* Not present? */
		if (lenwrit == 0) {
			warn0("Chunk no longer available?");
			goto err0;
		}

		skiplen += lenwrit;
	}

	/* If we have a trailer, write it. */
	if (cce->trailer != NULL) {
		lenwrit = writetape_write(cookie, cce->trailer,
		    cce->ccr->tlen);

		/* Error? */
		if (lenwrit < 0)
			goto err0;

		skiplen += lenwrit;
	}

	/* Success! */
	return (skiplen);

err0:
	/* Failure! */
	return (-1);
}

/**
 * ccache_entry_writefile(cce, cookie, notrailer, fd):
 * Write data from the file descriptor ${fd} to the multitape with write
 * cookie ${cookie}, using the cache entry ${cce} as a hint about how data
 * is chunkified; and set up callbacks from the multitape layer so that the
 * cache entry will be updated with any further chunks and (if ${notrailer}
 * is zero) any trailer.  Return the length written.
 */
off_t
ccache_entry_writefile(CCACHE_ENTRY * cce, TAPE_W * cookie,
    int notrailer, int fd)
{
	off_t skiplen = 0;
	uint8_t * chunkbuf;
	size_t chunklen, cpos;
	size_t cnum;
	ssize_t lenwrit, lenread;
	uint8_t hbuf[32];

	/* If we have some chunks, allocate a buffer for verification. */
	if (cce->ccr->nch) {
		if ((chunkbuf = malloc(MAXCHUNK)) == NULL)
			goto err0;
	} else {
		chunkbuf = NULL;
	}

	/* Read chunk-sized blocks and write them if unchanged. */
	for (cnum = 0; cnum < cce->ccr->nch; cnum++) {
		/* Handle network activity if necessary. */
		if (network_select(0))
			goto err1;

		/* Decode a chunk. */
		chunklen = le32dec((cce->ccr->chp + cnum)->len);

		/* Sanity check. */
		if (chunklen > MAXCHUNK) {
			warn0("Cache entry is corrupt");
			break;
		}

		/*
		 * We can't go beyond the length which libarchive thinks the
		 * file is, even if the file has grown since when we called
		 * lstat on it and the cache is corrupt.
		 */
		if ((off_t)(skiplen + chunklen) > cce->size_new)
			break;

		/* Read until we've got the whole chunk. */
		for (cpos = 0; cpos < chunklen; cpos += lenread) {
			lenread = read(fd, chunkbuf + cpos, chunklen - cpos);
			if (lenread < 0) {
				warnp("reading file");
				goto err1;
			} else if (lenread == 0) {
				/*
				 * There's nothing wrong with the file being
				 * shorter than it used to be.
				 */
				break;
			}
		}

		/* If we hit EOF, we can't use this chunk. */
		if (cpos < chunklen)
			break;

		/* Compute the hash of the data we've read. */
		if (crypto_hash_data(CRYPTO_KEY_HMAC_CHUNK,
		    chunkbuf, chunklen, hbuf))
			goto err1;

		/* Is it different? */
		if (memcmp(hbuf, (cce->ccr->chp + cnum)->hash, 32))
			break;

		/* Ok, pass the chunk header to the multitape code. */
		lenwrit = writetape_writechunk(cookie, cce->ccr->chp + cnum);

		/* Error? */
		if (lenwrit < 0)
			goto err1;

		/*
		 * Chunk not present?  This can happen in here, since
		 * we don't verify that all the chunks are available
		 * during ccache_entry_start if the file has changed.
		 */
		if (lenwrit == 0)
			break;

		/* We've written the chunk; the caller can skip it. */
		skiplen += lenwrit;
	}

	/* Free chunk buffer. */
	free(chunkbuf);

	/* Record the number of chunks we wrote. */
	cce->ccr->nch = cnum;

	/* Update the inode number, file size, and modification time. */
	cce->ccr->ino = cce->ino_new;
	cce->ccr->size = cce->size_new;
	cce->ccr->mtime = cce->mtime_new;

	/* Ask the multitape layer to inform us about later chunks. */
	writetape_setcallback(cookie, callback_addchunk,
	    notrailer ? NULL : callback_addtrailer, cce->ccr);

	/* Success! */
	return (skiplen);

err1:
	free(chunkbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * ccache_entry_end(cache, cce, cookie, path, snaptime):
 * The archive entry is ending; clean up callbacks, insert the cache entry
 * into the cache if it isn't already present, and free memory.
 */
int
ccache_entry_end(CCACHE * cache, CCACHE_ENTRY * cce, TAPE_W * cookie,
    const char * path, time_t snaptime)
{
	size_t slen;

	/* Don't want any more callbacks. */
	writetape_setcallback(cookie, NULL, NULL, NULL);

	/*
	 * If the cache entry is stale and ccache_entry_writefile was
	 * never called, the cached chunks we have are probably not useful
	 * (the file was probably truncated to 0 bytes); so remove them.
	 */
	if ((cce->ino_new != cce->ccr->ino) ||
	    (cce->size_new != cce->ccr->size) ||
	    (cce->mtime_new != cce->ccr->mtime))
		cce->ccr->nch = 0;

	/*
	 * If the modification time is equal to or after the snapshot time,
	 * adjust the modification time to ensure that we will consider this
	 * file to be "modified" the next time we see it.
	 */
	if (cce->ccr->mtime >= snaptime)
		cce->ccr->mtime = snaptime - 1;

	/* This cache entry is in use and should not be expired yet. */
	cce->ccr->age = 0;

	/*
	 * If the entry is worth keeping, make sure it's in the cache;
	 * otherwise, free it.
	 */
	if ((cce->ccr->nch != 0) || (cce->ccr->tlen != 0)) {
		if (cce->ccrp == NULL) {
			slen = strlen(path);
			if (patricia_insert(cache->tree,
			    (const uint8_t *)path, slen, cce->ccr))
				goto err1;
		}
	} else {
		/*
		 * Don't need to free ztrailer or chp -- if we got here they
		 * must both be NULL.
		 */
		free(cce->ccr);
	}

	/* Free the cache entry cookie. */
	free(cce->trailer);
	free(cce);

	/* Success! */
	return (0);

err1:
	if (cce->ccr->flags & CCR_ZTRAILER_MALLOC)
		free(cce->ccr->ztrailer);
	if (cce->ccr->nchalloc)
		free(cce->ccr->chp);
	free(cce->ccr);
	free(cce->trailer);
	free(cce);

	/* Failure! */
	return (-1);
}

/**
 * ccache_entry_free(cce, cookie):
 * Free the cache entry and cancel callbacks from the multitape layer.
 */
void
ccache_entry_free(CCACHE_ENTRY * cce, TAPE_W * cookie)
{

	if (cce == NULL)
		return;

	/* Don't want any more callbacks. */
	writetape_setcallback(cookie, NULL, NULL, NULL);

	/* If the record isn't in the tree, free it. */
	if (cce->ccrp == NULL) {
		if (cce->ccr->flags & CCR_ZTRAILER_MALLOC)
			free(cce->ccr->ztrailer);
		if (cce->ccr->nchalloc)
			free(cce->ccr->chp);
		free(cce->ccr);
	}

	/* Free the cache entry cookie. */
	free(cce->trailer);
	free(cce);
}
