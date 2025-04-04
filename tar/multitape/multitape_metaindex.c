#include "platform.h"

#include <sys/types.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chunks.h"
#include "crypto.h"
#include "crypto_verify_bytes.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape_internal.h"

/**
 * Metaindex format:
 * <32-bit little-endian header stream index length>
 * <header stream index>
 * <32-bit little-endian chunk index stream index length>
 * <chunk index stream index>
 * <32-bit little-endian trailer stream index length>
 * <trailer stream index>
 */

/**
 * multitape_metaindex_fragname(namehash, fragnum, fraghash):
 * Compute ${fraghash = SHA256(namehash || fragnum)}, which is the name
 * of the file containing the ${fragnum}'th part of the index corresponding to
 * the metadata with file name ${namehash}.
 */
void
multitape_metaindex_fragname(const uint8_t namehash[32], uint32_t fragnum,
    uint8_t fraghash[32])
{
	uint8_t fragnum_le[4];

	/* Use a platform-agnostic format for fragnum. */
	le32enc(fragnum_le, fragnum);

	/* SHA256(namehash || fragnum). */
	if (crypto_hash_data_2(CRYPTO_KEY_HMAC_SHA256, namehash, 32,
	    fragnum_le, 4, fraghash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		exit(1);
	}
}

/**
 * multitape_metaindex_put(S, C, mind, mdat):
 * Store the provided archive metaindex, and update the archive metadata
 * with the metaindex parameters.  Call chunks_write_extrastats on ${C} and
 * the length(s) of file(s) containing the metaindex.
 */
int
multitape_metaindex_put(STORAGE_W * S, CHUNKS_W * C,
    struct tapemetaindex * mind, struct tapemetadata * mdat)
{
	uint8_t hbuf[32];	/* HMAC of tape name. */
	uint8_t * buf;		/* Encoded metaindex. */
	size_t buflen;		/* Encoded metaindex size. */
	uint64_t totalsize;
	uint8_t * p;
	size_t fragnum;		/* not uint32_t, to avoid integer overflow. */
	size_t fraglen;
	uint8_t fraghash[32];

	/* Make sure that the stream indices fit into uint32_t. */
	if ((mind->hindexlen > UINT32_MAX) ||
	    (mind->cindexlen > UINT32_MAX) ||
	    (mind->tindexlen > UINT32_MAX)) {
		warn0("Archive index component too large");
		goto err0;
	}

	/* Accumulate size in a uint64_t so that we can check for overflow. */
	totalsize = 12;
	totalsize += (uint64_t)(mind->hindexlen);
	totalsize += (uint64_t)(mind->cindexlen);
	totalsize += (uint64_t)(mind->tindexlen);
	if (totalsize > SIZE_MAX) {
		errno = ENOMEM;
		goto err0;
	} else {
		buflen = (size_t)totalsize;
	}

	/* Allocate memory. */
	if ((p = buf = malloc(buflen)) == NULL)
		goto err0;

	/* Copy the header index into the buffer. */
	le32enc(p, (uint32_t)mind->hindexlen);
	p += 4;
	if (mind->hindexlen > 0)
		memcpy(p, mind->hindex, mind->hindexlen);
	p += mind->hindexlen;

	/* Copy the chunk index into the buffer. */
	le32enc(p, (uint32_t)mind->cindexlen);
	p += 4;
	if (mind->cindexlen > 0)
		memcpy(p, mind->cindex, mind->cindexlen);
	p += mind->cindexlen;

	/* Copy the trailer index into the buffer. */
	le32enc(p, (uint32_t)mind->tindexlen);
	p += 4;
	if (mind->tindexlen > 0)
		memcpy(p, mind->tindex, mind->tindexlen);
	p += mind->tindexlen;

	(void)p; /* not used beyond this point. */

	/* Compute hash of tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)mdat->name, strlen(mdat->name), hbuf))
		goto err1;

	/* Store the archive metaindex. */
	for (fragnum = 0; fragnum * MAXIFRAG < buflen; fragnum++) {
		fraglen = buflen - fragnum * MAXIFRAG;
		if (fraglen > MAXIFRAG)
			fraglen = MAXIFRAG;
		multitape_metaindex_fragname(hbuf, (uint32_t)fragnum, fraghash);
		if (storage_write_file(S, buf + fragnum * MAXIFRAG,
		    fraglen, 'i', fraghash))
			goto err1;
		chunks_write_extrastats(C, fraglen);
	}

	/* Compute the hash of the metaindex. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256,
	    buf, buflen, mdat->indexhash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err1;
	}

	/* Store the metaindex length in the metadata structure. */
	mdat->indexlen = buflen;

	/* Free metaindex buffer. */
	free(buf);

	/* Success! */
	return (0);

err1:
	free(buf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metaindex_get(S, C, mind, mdat, quiet):
 * Read and parse the metaindex for the archive associated with ${mdat}.  If
 * ${C} is non-NULL, call chunks_stats_extrastats on ${C} and the length(s)
 * of file(s) containing the metaindex.  Return 0 on success, 1 if the
 * metaindex file does not exist, 2 if the metaindex file is corrupt, or -1
 * on error.
 */
int
multitape_metaindex_get(STORAGE_R * S, CHUNKS_S * C,
    struct tapemetaindex * mind, const struct tapemetadata * mdat,
    int quiet)
{
	uint8_t hbuf[32];
	uint8_t indexhbuf[32];
	uint8_t * mbuf;
	size_t fragnum;		/* not uint32_t, to avoid integer overflow. */
	size_t fraglen;
	uint8_t fraghash[32];
	uint8_t * buf;	/* Start of unparsed part of index. */
	size_t buflen;	/* Unparsed index length. */

	/* Compute the hash of the tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)mdat->name, strlen(mdat->name), hbuf))
		goto err0;

	/* Allocate space for tape metaindex. */
	if (mdat->indexlen > SIZE_MAX) {
		errno = ENOMEM;
		goto err0;
	}
	if ((mbuf = malloc((size_t)mdat->indexlen)) == NULL)
		goto err0;

	/* Read the archive metaindex. */
	for (fragnum = 0; fragnum * MAXIFRAG < mdat->indexlen; fragnum++) {
		fraglen = (size_t)mdat->indexlen - fragnum * MAXIFRAG;
		if (fraglen > MAXIFRAG)
			fraglen = MAXIFRAG;
		multitape_metaindex_fragname(hbuf, (uint32_t)fragnum, fraghash);
		switch (storage_read_file(S, mbuf + fragnum * MAXIFRAG,
		    fraglen, 'i', fraghash)) {
		case -1:
			/* Error reading file. */
			warnp("Error reading archive index");
			goto err1;
		case 1:
			/* ENOENT. */
			goto notpresent;
		case 2:
			/* Corrupt file. */
			goto corrupt0;
		}
		if (C != NULL)
			chunks_stats_extrastats(C, fraglen);
	}

	/* Make sure the index matches the hash provided. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256,
	    mbuf, (size_t)mdat->indexlen, indexhbuf)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err1;
	}
	if (crypto_verify_bytes(mdat->indexhash, indexhbuf, 32))
		goto corrupt0;

	/* We haven't parsed any of the index yet. */
	buf = mbuf;
	buflen = (size_t)mdat->indexlen;

	/* Extract header stream index. */
	if (buflen < 4)
		goto corrupt0;
	mind->hindexlen = le32dec(buf);
	buf += 4;
	buflen -= 4;

	/* Sanity check. */
	if (buflen < mind->hindexlen)
		goto corrupt0;

	/* Copy the header index into a new buffer. */
	if (((mind->hindex = malloc(mind->hindexlen)) == NULL) &&
	    (mind->hindexlen > 0))
		goto err1;
	if (mind->hindexlen > 0)
		memcpy(mind->hindex, buf, mind->hindexlen);
	buf += mind->hindexlen;
	buflen -= mind->hindexlen;

	/* Extract chunk index stream index. */
	if (buflen < 4)
		goto corrupt1;
	mind->cindexlen = le32dec(buf);
	buf += 4;
	buflen -= 4;

	/* Sanity check. */
	if (buflen < mind->cindexlen)
		goto corrupt1;

	/* Copy the chunk index into a new buffer. */
	if (((mind->cindex = malloc(mind->cindexlen)) == NULL) &&
	    (mind->cindexlen > 0))
		goto err2;
	if (mind->cindexlen > 0)
		memcpy(mind->cindex, buf, mind->cindexlen);
	buf += mind->cindexlen;
	buflen -= mind->cindexlen;

	/* Extract trailer stream index. */
	if (buflen < 4)
		goto corrupt2;
	mind->tindexlen = le32dec(buf);
	buf += 4;
	buflen -= 4;

	/* Sanity check. */
	if (buflen < mind->tindexlen)
		goto corrupt2;

	/* Copy the trailer index into a new buffer. */
	if (((mind->tindex = malloc(mind->tindexlen)) == NULL) &&
	    (mind->tindexlen > 0))
		goto err3;
	if (mind->tindexlen > 0)
		memcpy(mind->tindex, buf, mind->tindexlen);
	buf += mind->tindexlen;
	buflen -= mind->tindexlen;

	/* Sanity check. */
	if (buflen != 0)
		goto corrupt3;

	(void)buf; /* not used beyond this point. */

	/* Free metaindex buffer. */
	free(mbuf);

	/* Success! */
	return (0);

corrupt3:
	free(mind->tindex);
corrupt2:
	free(mind->cindex);
corrupt1:
	free(mind->hindex);
corrupt0:
	if (quiet == 0)
		warn0("Archive index is corrupt");
	free(mbuf);

	/* File is corrupt. */
	return (2);

notpresent:
	if (quiet == 0)
		warn0("Archive index does not exist: Run --fsck");
	free(mbuf);

	/* ENOENT. */
	return (1);

err3:
	free(mind->cindex);
err2:
	free(mind->hindex);
err1:
	free(mbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metaindex_free(mind):
 * Free pointers within ${mind} (but not ${mind} itself).
 */
void
multitape_metaindex_free(struct tapemetaindex * mind)
{

	/* Behave consistently with free(NULL). */
	if (mind == NULL)
		return;

	/* Clean up. */
	free(mind->tindex);
	free(mind->cindex);
	free(mind->hindex);
}

/**
 * multitape_metaindex_delete(S, C, mdat):
 * Delete the metaindex file associated with the provided metadata.  Call
 * chunks_delete_extrastats on ${C} and the length(s) of file(s) containing
 * the metaindex.
 */
int
multitape_metaindex_delete(STORAGE_D * S, CHUNKS_D * C,
    struct tapemetadata * mdat)
{
	uint8_t hbuf[32];
	size_t fragnum;		/* not uint32_t, to avoid integer overflow. */
	size_t fraglen;
	uint8_t fraghash[32];

	/* Compute the hash of the tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)mdat->name, strlen(mdat->name), hbuf))
		goto err0;

	for (fragnum = 0; fragnum * MAXIFRAG < mdat->indexlen; fragnum++) {
		fraglen = (size_t)mdat->indexlen - fragnum * MAXIFRAG;
		if (fraglen > MAXIFRAG)
			fraglen = MAXIFRAG;
		multitape_metaindex_fragname(hbuf, (uint32_t)fragnum, fraghash);
		if (storage_delete_file(S, 'i', fraghash))
			goto err0;
		chunks_delete_extrastats(C, fraglen);
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
