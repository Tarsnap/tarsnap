#ifndef _MULTITAPE_INTERNAL_H_
#define _MULTITAPE_INTERNAL_H_

#include <stdint.h>

#include "chunks.h"
#include "ctassert.h"
#include "storage.h"

/* On-disk chunk header structure.  Integers are little-endian. */
struct chunkheader {
	uint8_t hash[32];
	uint8_t len[4];
	uint8_t zlen[4];
};
CTASSERT(sizeof(struct chunkheader) == 40);

/* On-disk entry header structure. */
struct entryheader {
	uint8_t hlen[4];
	uint8_t clen[8];
	uint8_t tlen[4];
};
CTASSERT(sizeof(struct entryheader) == 16);

/* Archive metadata structure. */
struct tapemetadata {
	char * name;
	time_t ctime;
	int argc;
	char ** argv;
	uint8_t indexhash[32];
	uint64_t indexlen;
	size_t metadatalen;	/* Filled in by _get. */
};

/* Unpacked archive metaindex structure. */
struct tapemetaindex {
	uint8_t * hindex;
	size_t hindexlen;
	uint8_t * cindex;
	size_t cindexlen;
	uint8_t * tindex;
	size_t tindexlen;
};

/*
 * Maximum chunk size.  This is chosen so that after deflating (which might
 * add up to 0.1% + 13 bytes to the size) and adding cryptographic wrapping
 * (which will add 296 bytes) the final maximum file size is <= 2^18.
 */
#define	MAXCHUNK	261120

/*
 * Maximum index fragment size.  The metaindex is stored as a series of
 * fragments of this length plus a final fragment containing whatever is
 * left.
 */
#define MAXIFRAG	MAXCHUNK

/**
 * multitape_cleanstate(cachedir, machinenum, key):
 * Complete any pending checkpoint or commit.  The value ${key} should be 0
 * if the write access key should be used to sign a commit request, or 1 if
 * the delete access key should be used.
 */
int multitape_cleanstate(const char *, uint64_t, uint8_t);

/**
 * multitape_checkpoint(cachedir, machinenum, seqnum):
 * Create a checkpoint in the current write transaction.
 */
int multitape_checkpoint(const char *, uint64_t, const uint8_t[32]);

/**
 * multitape_commit(cachedir, machinenum, seqnum, key):
 * Commit the most recent transaction.  The value ${key} is defined as in
 * multitape_cleanstate.
 */
int multitape_commit(const char *, uint64_t, const uint8_t[32], uint8_t);

/**
 * multitape_chunkiter_tmd(S, C, tmd, func, cookie, quiet):
 * Call ${func} on ${cookie} and each struct chunkheader involved in the
 * archive associated with the metadata ${tmd}.  If ${C} is non-NULL, call
 * chunks_stats_extrastats on ${C} and the length of each metadata fragment.
 * If ${quiet}, don't print any warnings about corrupt or missing files.
 * Return 0 (success), 1 (a required file is missing), 2 (a required file is
 * corrupt), -1 (error), or the first non-zero value returned by ${func}.
 */
int multitape_chunkiter_tmd(STORAGE_R *, CHUNKS_S *,
    const struct tapemetadata *,
    int(void *, struct chunkheader *), void *, int);

/**
 * multitape_metadata_ispresent(S, tapename):
 * Return 1 if there is already a metadata file for the specified archive
 * name, 0 if not, or -1 on error.
 */
int multitape_metadata_ispresent(STORAGE_W *, const char *);

/**
 * multitape_metadata_put(S, C, mdat, extrastats):
 * Store archive metadata.  Call chunks_write_extrastats on ${C} and the
 * metadata file length if ${extrastats} != 0.
 */
int multitape_metadata_put(STORAGE_W *, CHUNKS_W *, struct tapemetadata *,
    int);

/**
 * multitape_metadata_get_byhash(S, C, mdat, tapehash, quiet):
 * Read and parse metadata for the archive for which the metadata file is
 * named ${tapehash}.  If ${C} is non-NULL, call chunks_stats_extrastats on
 * ${C} and the length of the metadata file.  If ${quiet}, don't print any
 * warnings about corrupt or missing files.  Return 0 on success, 1 if the
 * metadata file does not exist, 2 if the metadata file is corrupt, or -1 on
 * error.
 */
int multitape_metadata_get_byhash(STORAGE_R *, CHUNKS_S *,
    struct tapemetadata *, const uint8_t[32], int);

/**
 * multitape_metadata_get_byname(S, C, mdat, tapename, quiet):
 * Read and parse metadata for the archive named ${tapename}.  If ${C} is
 * non-NULL, call chunks_stats_extrastats on ${C} and the length of the
 * metadata file.  If ${quiet}, don't print any warnings about corrupt or
 * missing files.  Return 0 on success, 1 if the metadata file does not
 * exist, 2 if the metadata file is corrupt, or -1 on error.
 */
int multitape_metadata_get_byname(STORAGE_R *, CHUNKS_S *,
    struct tapemetadata *, const char *, int);

/**
 * multitape_metadata_free(mdat):
 * Free pointers within ${mdat} (but not ${mdat} itself).
 */
void multitape_metadata_free(struct tapemetadata *);

/**
 * multitape_metadata_recrypt(obuf, obuflen, nbuf, nbuflen):
 * Decrypt and re-encrypt the provided metadata file.
 */
int multitape_metadata_recrypt(uint8_t *, size_t, uint8_t **, size_t *);

/**
 * multitape_metadata_delete(S, C, mdat):
 * Delete specified metadata file; ${mdat} must have been initialized by a
 * call to multitape_metadata_get_by(hash|name).  Call
 * chunks_delete_extrastats on ${C} and the metadata file length.
 */
int multitape_metadata_delete(STORAGE_D *, CHUNKS_D *, struct tapemetadata *);

/**
 * Compute fraghash = SHA256(namehash || fragnum), which is the name of the
 * file containing the fragnum'th part of the index corresponding to the
 * metadata with file name namehash.
 */
void multitape_metaindex_fragname(const uint8_t[32], uint32_t, uint8_t[32]);

/**
 * multitape_metaindex_put(S, C, mind, mdat, extrastats):
 * Store the provided archive metaindex, and update the archive metadata
 * with the metaindex parameters.  Call chunks_write_extrastats on ${C} and
 * the length(s) of file(s) containing the metaindex if ${extrastats} != 0.
 */
int multitape_metaindex_put(STORAGE_W *, CHUNKS_W *, struct tapemetaindex *,
    struct tapemetadata *, int);

/**
 * multitape_metaindex_get(S, C, mind, mdat, quiet):
 * Read and parse the metaindex for the archive associated with ${mdat}.  If
 * ${C} is non-NULL, call chunks_stats_extrastats on ${C} and the length(s)
 * of file(s) containing the metaindex.  Return 0 on success, 1 if the
 * metaindex file does not exist, 2 if the metaindex file is corrupt, or -1
 * on error.
 */
int multitape_metaindex_get(STORAGE_R *, CHUNKS_S *, struct tapemetaindex *,
    const struct tapemetadata *, int);

/**
 * multitape_metaindex_free(mind):
 * Free pointers within ${mind} (but not ${mind} itself).
 */
void multitape_metaindex_free(struct tapemetaindex *);

/**
 * multitape_metaindex_delete(S, C, mdat):
 * Delete the metaindex file associated with the provided metadata.  Call
 * chnks_delete_extrastats on ${C} and the length(s) of file(s) containing
 * the metaindex.
 */
int multitape_metaindex_delete(STORAGE_D *, CHUNKS_D *,
    struct tapemetadata *);

/**
 * multitape_lock(cachedir):
 * Lock the given cache directory using lockf(3) or flock(2); return the file
 * descriptor of the lock file, or -1 on error.
 */
int multitape_lock(const char *);

/**
 * multitape_sequence(cachedir, lastseq):
 * Set ${lastseq} to the sequence number of the last committed transaction
 * in the cache directory ${cachedir}, or 0 if no transactions have ever been
 * committed.
 */
int multitape_sequence(const char *, uint8_t[32]);

#endif /* !_MULTITAPE_INTERNAL_H_ */
