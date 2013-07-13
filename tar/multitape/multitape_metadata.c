#include "bsdtar_platform.h"

#include <sys/types.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "crypto_verify_bytes.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape_internal.h"

/**
 * Metadata format:
 * <NUL-terminated name>
 * <64-bit little-endian creation time>
 * <32-bit little-endian argc>
 * argc * <NUL-terminated argv entry>
 * SHA256(metaindex)
 * <64-bit metaindex length>
 * RSA_SIGN(all the metadata before this signature)
 */

static int multitape_metadata_enc(const struct tapemetadata *, uint8_t **,
    size_t *);
static int multitape_metadata_dec(struct tapemetadata *, uint8_t *, size_t);
static int multitape_metadata_get(STORAGE_R *, CHUNKS_S *,
    struct tapemetadata *, const uint8_t[32], const char *, int);

/**
 * multitape_metadata_ispresent(S, tapename):
 * Return 1 if there is already a metadata file for the specified archive
 * name, 0 if not, or -1 on error.
 */
int
multitape_metadata_ispresent(STORAGE_W * S, const char * tapename)
{
	uint8_t hbuf[32];	/* HMAC of tapename. */

	/* Compute the hash of the tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (const uint8_t *)tapename, strlen(tapename), hbuf))
		goto err0;

	/* Ask the storage layer if the metadata file exists. */
	return (storage_write_fexist(S, 'm', hbuf));

err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metadata_enc(mdat, bufp, buflenp):
 * Encode a struct tapemetadata into a buffer.  Return the buffer and its
 * length via ${bufp} and ${buflenp} respectively.
 */
static int
multitape_metadata_enc(const struct tapemetadata * mdat, uint8_t ** bufp,
    size_t * buflenp)
{
	uint8_t * buf;		/* Encoded metadata. */
	size_t buflen;		/* Encoded metadata size. */
	uint8_t * p;
	int i;

	/* Add up the lengths of various pieces of metadata. */
	buflen = strlen(mdat->name) + 1;	/* name */
	buflen += 8;				/* ctime */
	buflen += 4;				/* argc */
	for (i = 0; i < mdat->argc; i++)	/* argv */
		buflen += strlen(mdat->argv[i]) + 1;
	buflen += 32;				/* indexhash */
	buflen += 8;				/* index length */
	buflen += 256;				/* 2048-bit RSA signature */

	/* Guard against API ambiguity. */
	if (buflen == (size_t)(-1)) {
		errno = ENOMEM;
		goto err0;
	}

	/* Allocate memory. */
	if ((p = buf = malloc(buflen)) == NULL)
		goto err0;

	/* Copy name. */
	memcpy(p, mdat->name, strlen(mdat->name) + 1);
	p += strlen(mdat->name) + 1;

	/* Encode ctime and argc. */
	le64enc(p, mdat->ctime);
	p += 8;
	le32enc(p, mdat->argc);
	p += 4;

	/* Copy argv. */
	for (i = 0; i < mdat->argc; i++) {
		memcpy(p, mdat->argv[i], strlen(mdat->argv[i]) + 1);
		p += strlen(mdat->argv[i]) + 1;
	}

	/* Copy index hash. */
	memcpy(p, mdat->indexhash, 32);
	p += 32;

	/* Encode index length. */
	le64enc(p, mdat->indexlen);
	p += 8;

	/* Generate signature. */
	if (crypto_rsa_sign(CRYPTO_KEY_SIGN_PRIV, buf, p - buf, p, 256))
		goto err1;

	/* Return buffer and length. */
	*bufp = buf;
	*buflenp = buflen;

	/* Success! */
	return (0);

err1:
	free(buf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metadata_put(S, C, mdat, extrastats):
 * Store archive metadata.  Call chunks_write_extrastats on ${C} and the
 * metadata file length if ${extrastats} != 0.
 */
int
multitape_metadata_put(STORAGE_W * S, CHUNKS_W * C,
    struct tapemetadata * mdat, int extrastats)
{
	uint8_t hbuf[32];	/* HMAC of tape name. */
	uint8_t * buf;		/* Encoded metadata. */
	size_t buflen;		/* Encoded metadata size. */

	/* Construct metadata file. */
	if (multitape_metadata_enc(mdat, &buf, &buflen))
		goto err0;

	/* Compute hash of tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)mdat->name, strlen(mdat->name), hbuf))
		goto err1;

	/* Store the archive metadata. */
	if (storage_write_file(S, buf, buflen, 'm', hbuf))
		goto err1;
	if (extrastats)
		chunks_write_extrastats(C, buflen);

	/* Free metadata buffer. */
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
 * multitape_metadata_dec(mdat, buf, buflen):
 * Parse a buffer into a struct tapemetadata.  Return 0 on success, 1 if the
 * metadata is corrupt, or -1 on error.
 */
static int
multitape_metadata_dec(struct tapemetadata * mdat, uint8_t * buf,
    size_t buflen)
{
	uint8_t * p;
	size_t i;
	int arg;

	/* Start at the beginning... */
	p = buf;

	/* Make sure the archive name is NUL-terminated. */
	for (i = 0; i < buflen; i++)
		if (p[i] == '\0')
			break;
	if (i == buflen)
		goto bad0;

	/* Copy the archive name and move on to next field. */
	if ((mdat->name = strdup((char *)p)) == NULL)
		goto err0;
	buflen -= strlen((char *)p) + 1;
	p += strlen((char *)p) + 1;

	/* Parse ctime and argc. */
	if (buflen < 8)
		goto bad1;
	mdat->ctime = le64dec(p);
	buflen -= 8;
	p += 8;
	if (buflen < 4)
		goto bad1;
	mdat->argc = le32dec(p);
	buflen -= 4;
	p += 4;

	/* Allocate space for argv. */
	if ((mdat->argv = malloc(mdat->argc * sizeof(char *))) == NULL)
		goto err1;

	/* Parse argv. */
	for (arg = 0; arg < mdat->argc; arg++)
		mdat->argv[arg] = NULL;
	for (arg = 0; arg < mdat->argc; arg++) {
		/* Make sure argument is NUL-terminated. */
		for (i = 0; i < buflen; i++)
			if (p[i] == '\0')
				break;
		if (i == buflen)
			goto bad2;

		/* Copy argument and move on to next field. */
		if ((mdat->argv[arg] = strdup((char *)p)) == NULL)
			goto err2;
		buflen -= strlen((char *)p) + 1;
		p += strlen((char *)p) + 1;
	}

	/* Copy indexhash. */
	if (buflen < 32)
		goto bad2;
	memcpy(mdat->indexhash, p, 32);
	buflen -= 32;
	p += 32;

	/* Parse index length. */
	if (buflen < 8)
		goto bad2;
	mdat->indexlen = le64dec(p);
	buflen -= 8;
	p += 8;

	/* Validate signature. */
	if (buflen < 256)
		goto bad2;
	switch (crypto_rsa_verify(CRYPTO_KEY_SIGN_PUB,
	    buf, p - buf, p, 256)) {
	case -1:
		/* Error in crypto_rsa_verify. */
		goto err2;
	case 1:
		/* Bad signature. */
		goto bad2;
	case 0:
		/* Signature is good. */
		break;
	}
	buflen -= 256;
	p += 256;

	/* We should be at the end of the metadata now. */
	if (buflen != 0)
		goto bad2;

	/* Success! */
	return (0);

bad2:
	for (arg = 0; arg < mdat->argc; arg++)
		free(mdat->argv[arg]);
	free(mdat->argv);
bad1:
	free(mdat->name);
bad0:
	/* Metadata is corrupt. */
	return (1);

err2:
	for (arg = 0; arg < mdat->argc; arg++)
		free(mdat->argv[arg]);
	free(mdat->argv);
err1:
	free(mdat->name);
err0:
	/* Failure! */
	return (-1);
}

static int
multitape_metadata_get(STORAGE_R * S, CHUNKS_S * C,
    struct tapemetadata * mdat,
    const uint8_t tapehash[32], const char * tapename, int quiet)
{
	uint8_t hbuf[32];
	uint8_t * mbuf;
	size_t mdlen;

	/* Read the tape metadata. */
	switch (storage_read_file_alloc(S, &mbuf, &mdlen, 'm', tapehash)) {
	case -1:
		/* Internal error. */
		goto err1;
	case 1:
		/* ENOENT. */
		goto notpresent;
	case 2:
		/* Corrupt metadata file. */
		goto corrupt;
	}

	/* Adjust chunk statistics. */
	if (C != NULL)
		chunks_stats_extrastats(C, mdlen);

	/* Parse the tape metadata. */
	switch (multitape_metadata_dec(mdat, mbuf, mdlen)) {
	case 1:
		/* Metadata is corrupt. */
		goto corrupt1;
	case -1:
		/* Error. */
		goto err2;
	}

	/* Store metadata length. */
	mdat->metadatalen = mdlen;

	/* Free tape metadata. */
	free(mbuf);

	/*
	 * Make sure the name stored in the archive metadata matches the
	 * name of the metadata file.
	 */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)mdat->name, strlen(mdat->name), hbuf))
		goto err0;
	if (crypto_verify_bytes(tapehash, hbuf, 32))
		goto corrupt;

	/* Success! */
	return (0);

corrupt1:
	free(mbuf);
corrupt:
	if (quiet == 0) {
		if (tapename)
			warn0("Archive metadata is corrupt: %s", tapename);
		else
			warn0("Archive metadata file is corrupt");
	}

	/* File is corrupt. */
	return (2);

notpresent:
	if (quiet == 0) {
		if (tapename)
			warn0("Archive does not exist: %s", tapename);
		else
			warn0("Cannot read archive metadata file");
	}

	/* ENOENT. */
	return (1);

err2:
	free(mbuf);
err1:
	warnp("Error reading archive metadata");
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metadata_get_byhash(S, C, mdat, tapehash, quiet):
 * Read and parse metadata for the archive for which the metadata file is
 * named ${tapehash}.  If ${C} is non-NULL, call chunks_stats_extrastats on
 * ${C} and the length of the metadata file.  If ${quiet}, don't print any
 * warnings about corrupt or missing files.  Return 0 on success, 1 if the
 * metadata file does not exist, 2 if the metadata file is corrupt, or -1 on
 * error.
 */
int multitape_metadata_get_byhash(STORAGE_R * S, CHUNKS_S * C,
    struct tapemetadata * mdat, const uint8_t tapehash[32], int quiet)
{

	/* Let multitape_metadata_get do the work. */
	return (multitape_metadata_get(S, C, mdat, tapehash, NULL, quiet));
}

/**
 * multitape_metadata_get_byname(S, C, mdat, tapename, quiet):
 * Read and parse metadata for the archive named ${tapename}.  If ${C} is
 * non-NULL, call chunks_stats_extrastats on ${C} and the length of the
 * metadata file.  If ${quiet}, don't print any warnings about corrupt or
 * missing files.  Return 0 on success, 1 if the metadata file does not
 * exist, 2 if the metadata file is corrupt, or -1 on error.
 */
int multitape_metadata_get_byname(STORAGE_R * S, CHUNKS_S * C,
    struct tapemetadata * mdat, const char * tapename, int quiet)
{
	uint8_t hbuf[32];

	/* Compute the hash of the tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (const uint8_t *)tapename, strlen(tapename), hbuf))
		goto err0;

	/* Let multitape_metadata_get do the work. */
	return (multitape_metadata_get(S, C, mdat, hbuf, tapename, quiet));

err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metadata_free(mdat):
 * Free pointers within ${mdat} (but not ${mdat} itself).
 */
void
multitape_metadata_free(struct tapemetadata * mdat)
{
	int arg;

	/* Be consistent with free(NULL). */
	if (mdat == NULL)
		return;

	/* Free arguments. */
	for (arg = 0; arg < mdat->argc; arg++)
		free(mdat->argv[arg]);
	free(mdat->argv);

	/* Free archive name. */
	free(mdat->name);
}

/**
 * multitape_metadata_recrypt(obuf, obuflen, nbuf, nbuflen):
 * Decrypt and re-encrypt the provided metadata file.
 */
int
multitape_metadata_recrypt(uint8_t * obuf, size_t obuflen, uint8_t ** nbuf,
    size_t * nbuflen)
{
	struct tapemetadata mdat;

	/* Parse the metadata file. */
	switch (multitape_metadata_dec(&mdat, obuf, obuflen)) {
	case 1:
		warn0("Metadata file is corrupt");
		goto err0;
	case -1:
		warnp("Error parsing metadata file");
		goto err0;
	}

	/* Construct a new metadata file. */
	if (multitape_metadata_enc(&mdat, nbuf, nbuflen)) {
		warnp("Error constructing metadata file");
		goto err1;
	}

	/* Free the metadata we parsed. */
	multitape_metadata_free(&mdat);

	/* Success! */
	return (0);

err1:
	multitape_metadata_free(&mdat);
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_metadata_delete(S, C, mdat):
 * Delete specified metadata file; ${mdat} must have been initialized by a
 * call to multitape_metadata_get_by(hash|name).  Call
 * chunks_delete_extrastats on ${C} and the metadata file length.
 */
int
multitape_metadata_delete(STORAGE_D * S, CHUNKS_D * C,
    struct tapemetadata * mdat)
{
	uint8_t hbuf[32];

	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)mdat->name, strlen(mdat->name), hbuf))
		goto err0;
	if (storage_delete_file(S, 'm', hbuf))
		goto err0;
	chunks_delete_extrastats(C, mdat->metadatalen);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
