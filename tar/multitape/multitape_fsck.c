#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chunks.h"
#include "crypto.h"
#include "hexify.h"
#include "imalloc.h"
#include "multitape_internal.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape.h"

static size_t findinlist(const uint8_t[32], const uint8_t *, size_t);
static int callback_chunkref(void *, struct chunkheader *);
static int deletearchive(STORAGE_D *, struct tapemetadata *);
static int phase1(uint64_t, STORAGE_D *, STORAGE_R *,
    struct tapemetadata ***, size_t *);
static int phase2(uint64_t, STORAGE_D *,
    struct tapemetadata **, size_t);
static CHUNKS_S * phase3(uint64_t, const char *);
static int phase4(STORAGE_D *, STORAGE_R *, CHUNKS_S *,
    struct tapemetadata **, size_t);
static int phase5(STORAGE_D *, CHUNKS_S *);

/**
 * findinlist(file, flist, nfiles):
 * Find ${file} in the sorted list ${flist} of length ${nfiles}.  Return its
 * location; or if it is not present, return ${nfiles}.
 */
static size_t
findinlist(const uint8_t file[32], const uint8_t * flist, size_t nfiles)
{
	size_t midpoint;
	int cmp;
	size_t rc;

	/* Handle 0 files and 1 file specially. */
	if (nfiles == 0) {
		/* Not found. */
		return (nfiles);
	}
	if (nfiles == 1) {
		if (memcmp(file, &flist[0], 32)) {
			/* Not found. */
			return (nfiles);
		} else {
			/* Got it! */
			return (0);
		}
	}

	/* Binary search. */
	midpoint = nfiles / 2;
	cmp = memcmp(file, &flist[midpoint * 32], 32);
	if (cmp < 0) {
		/* The file must be in the first half. */
		rc = findinlist(file, flist, midpoint);
		if (rc == midpoint) {
			/* Not found. */
			return (nfiles);
		} else {
			/* Got it! */
			return (rc);
		}
	} else if (cmp == 0) {
		/* Got it! */
		return (midpoint);
	} else {
		/* The file must be in the second half. */
		rc = findinlist(file, &flist[(midpoint + 1) * 32],
		    nfiles - (midpoint + 1));
		if (rc == nfiles - (midpoint + 1)) {
			/* Not found. */
			return (nfiles);
		} else {
			/* Got it! */
			return (rc + (midpoint + 1));
		}
	}
}

/**
 * callback_chunkref(cookie, ch):
 * Call chunks_stats_addchunk on the chunk stats cookie ${cookie} and the
 * chunk header ${ch}.
 */
static int
callback_chunkref(void * cookie, struct chunkheader * ch)
{
	CHUNKS_S * C = cookie;
	size_t len, zlen;

	/* Decode chunk header. */
	len = le32dec(ch->len);
	zlen = le32dec(ch->zlen);

	/* Notify the chunk layer that the current archive uses this chunk. */
	return (chunks_stats_addchunk(C, ch->hash, len, zlen));
}

/**
 * deletearchive(SD, tmd):
 * Delete the metadata and index for the specified archive.
 */
static int
deletearchive(STORAGE_D * SD, struct tapemetadata * tmd)
{
	uint8_t hbuf[32];
	size_t fragnum;
	uint8_t fraghash[32];

	/* Compute hash of tape name. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
	    (uint8_t *)tmd->name, strlen(tmd->name), hbuf))
		goto err0;

	/* Delete index fragments. */
	for (fragnum = 0; fragnum * MAXIFRAG < tmd->indexlen; fragnum++) {
		multitape_metaindex_fragname(hbuf, (uint32_t)fragnum, fraghash);
		if (storage_delete_file(SD, 'i', fraghash))
			goto err0;
	}

	/* Delete metadata file. */
	if (storage_delete_file(SD, 'm', hbuf))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * phase1(machinenum, SD, SR, mdatlist, nmdat):
 * Read the list of tape metadata files from the server, and parse each of
 * them, deleting any which are corrupt.  Set ${mdatlist} to point to a list
 * of pointers to metadata structures, and ${nmdat} to the number of (valid)
 * metadata files.
 */
static int
phase1(uint64_t machinenum, STORAGE_D * SD, STORAGE_R * SR,
    struct tapemetadata *** mdatlist, size_t * nmdat)
{
	uint8_t * flist;
	size_t nfiles;
	struct tapemetadata ** mdats;
	size_t nvalids;
	size_t file;
	struct tapemetadata * mdat;
	char fname[65];

	/* Report status. */
	fprintf(stdout, "Phase 1: Verifying metadata validity\n");

	/* Obtain a list of metadata files. */
	if (storage_directory_read(machinenum, 'm', 0, &flist, &nfiles))
		goto err0;

	/* Allocate space for nfiles tapemetadata structures. */
	if (IMALLOC(mdats, nfiles, struct tapemetadata *))
		goto err1;
	nvalids = 0;

	/* Scan through the list of metadata files, parsing each in turn. */
	for (file = 0; file < nfiles; file++) {
		if ((mdat = malloc(sizeof(struct tapemetadata))) == NULL)
			goto err2;

		switch (multitape_metadata_get_byhash(SR, NULL, mdat,
		    &flist[file * 32], 1)) {
		case -1:
			/* Internal error. */
			goto err3;
		case 0:
			/* Success. */
			mdats[nvalids] = mdat;
			nvalids += 1;
			break;
		case 1:
			/* That's weird, the file was there a moment ago. */
			warn0("Metadata file has vanished!");
			goto err3;
		case 2:
			/* Corrupt file -- delete it. */
			hexify(&flist[file * 32], fname, 32);
			fprintf(stdout,
			    "  Deleting corrupt metadata file: %s\n", fname);
			if (storage_delete_file(SD, 'm', &flist[file * 32]))
				goto err3;
			free(mdat);
			break;
		}
	}

	/* Free file list. */
	free(flist);

	/* Return list of tapemetadata structures. */
	*mdatlist = mdats;
	*nmdat = nvalids;

	/* Success! */
	return (0);

err3:
	free(mdat);
err2:
	for (file = 0; file < nvalids; file++) {
		multitape_metadata_free(mdats[file]);
		free(mdats[file]);
	}
	free(mdats);
err1:
	free(flist);
err0:
	/* Failure! */
	return (-1);
}

/**
 * phase2(machinenum, SD, mdatlist, nmdat):
 * Read the list of metaindex files from the server, and delete any metadata
 * or metaindex files for which there aren't corresponding metaindex or
 * metadata files.
 */
static int
phase2(uint64_t machinenum, STORAGE_D * SD,
    struct tapemetadata ** mdatlist, size_t nmdat)
{
	uint8_t * flist;
	size_t nfiles;
	size_t file;
	uint8_t hbuf[32];
	size_t fragnum;
	uint8_t fraghash[32];
	uint8_t * neededvec;
	char fname[65];

	/* Report status. */
	fprintf(stdout,
	    "Phase 2: Verifying metadata/metaindex consistency\n");

	/* Obtain a list of metaindex files. */
	if (storage_directory_read(machinenum, 'i', 0, &flist, &nfiles))
		goto err0;

	/*
	 * We make two passes through the metadata list: First we make sure
	 * that all the needed metaindex files exist, and remove any metadata
	 * files for which metaindex file(s) are missing; second, we record
	 * which metaindex files are needed and remove those which aren't.
	 */
	for (file = 0; file < nmdat; file++) {
		/* Compute hash of tape name. */
		if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
		    (uint8_t *)mdatlist[file]->name,
		    strlen(mdatlist[file]->name), hbuf))
			goto err1;

		for (fragnum = 0;
		    fragnum * MAXIFRAG < mdatlist[file]->indexlen;
		    fragnum++) {
			multitape_metaindex_fragname(hbuf, (uint32_t)fragnum,
			    fraghash);
			if (findinlist(fraghash, flist, nfiles) == nfiles) {
				fprintf(stdout,
				    "  Deleting archive"
				    " with corrupt index: %s\n",
				    mdatlist[file]->name);
				if (storage_delete_file(SD, 'm', hbuf))
					goto err1;

				/* This metadata doesn't exist any more. */
				multitape_metadata_free(mdatlist[file]);
				free(mdatlist[file]);
				mdatlist[file] = NULL;
				goto nextfile;
			}
		}
nextfile:	;
	}

	/* Allocate an array for keeping track of which files are needed. */
	if ((neededvec = malloc(nfiles)) == NULL)
		goto err1;
	memset(neededvec, 0, nfiles);

	/* Mark files as needed. */
	for (file = 0; file < nmdat; file++) {
		/* Skip deleted metadata. */
		if (mdatlist[file] == NULL)
			continue;

		/* Compute hash of tape name. */
		if (crypto_hash_data(CRYPTO_KEY_HMAC_NAME,
		    (uint8_t *)mdatlist[file]->name,
		    strlen(mdatlist[file]->name), hbuf))
			goto err2;

		for (fragnum = 0;
		    fragnum * MAXIFRAG < mdatlist[file]->indexlen;
		    fragnum++) {
			multitape_metaindex_fragname(hbuf, (uint32_t)fragnum,
			    fraghash);
			neededvec[findinlist(fraghash, flist, nfiles)] = 1;
		}
	}

	/* Delete any unneeded metaindex files. */
	for (file = 0; file < nfiles; file++) {
		if (neededvec[file] == 0) {
			hexify(&flist[file * 32], fname, 32);
			fprintf(stdout,
			    "  Deleting orphaned index fragment: %s\n",
			    fname);
			if (storage_delete_file(SD, 'i', &flist[file * 32]))
				goto err2;
		}
	}

	/* Free needed files vector. */
	free(neededvec);

	/* Free file list. */
	free(flist);

	/* Success! */
	return (0);

err2:
	free(neededvec);
err1:
	free(flist);
err0:
	/* Failure! */
	return (-1);
}

/**
 * phase3(machinenum, cachedir):
 * Read the list of chunks and prepare the chunks layer for fscking.
 */
CHUNKS_S *
phase3(uint64_t machinenum, const char * cachedir)
{

	/* Report status. */
	fprintf(stdout, "Phase 3: Reading chunk list\n");

	return (chunks_fsck_start(machinenum, cachedir));
}

/**
 * phase4(SD, SR, C, mdatlist, nmdat):
 * Verify that the index is not corrupt and that all needed chunks exist; and
 * reference-count the chunks (i.e., regenerate the chunk directory).
 */
static int
phase4(STORAGE_D * SD, STORAGE_R * SR, CHUNKS_S * C,
    struct tapemetadata ** mdatlist, size_t nmdat)
{
	size_t file;

	/* Report status. */
	fprintf(stdout, "Phase 4: Verifying archive completeness\n");

	/* Cache up to 100 bytes of blocks per chunk in the directory. */
	storage_read_set_cache_limit(SR, 100 * chunks_stats_getdirsz(C));

	/* Iterate through the archives. */
	for (file = 0; file < nmdat; file++) {
		/* Print progress. */
		fprintf(stdout, "  Archive %zu/%zu...\n", file + 1, nmdat);

		/* Skip deleted metadata. */
		if (mdatlist[file] == NULL)
			continue;

		/* The current archive hasn't referenced any chunks yet. */
		chunks_stats_zeroarchive(C);

		/* ... but one extra file (the metadata) has been used. */
		chunks_stats_extrastats(C, mdatlist[file]->metadatalen);

		/*
		 * Determine if all referenced chunks exist, and inform the
		 * chunk layer about said references.
		 */
		switch (multitape_chunkiter_tmd(SR, C, mdatlist[file],
		    callback_chunkref, C, 1)) {
		case -1:
			/* Internal error. */
			goto err0;
		case 0:
			/* Add "current archive" reference stats to total. */
			if (chunks_fsck_archive_add(C))
				goto err0;
			break;
		case 1:
			/* A non-existent file is referenced. */
			fprintf(stdout, "  Deleting broken archive: %s\n",
			    mdatlist[file]->name);
			if (deletearchive(SD, mdatlist[file]))
				goto err0;
			break;
		case 2:
			/* A file was corrupt. */
			fprintf(stdout, "  Deleting corrupt archive: %s\n",
			    mdatlist[file]->name);
			if (deletearchive(SD, mdatlist[file]))
				goto err0;
			break;
		}
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * phase5(SD, C):
 * Delete any chunks which aren't referenced by any archives.
 */
static int
phase5(STORAGE_D * SD, CHUNKS_S * C)
{

	/* Report status. */
	fprintf(stdout, "Phase 5: Identifying unreferenced chunks\n");

	return (chunks_fsck_deletechunks(C, SD));
}

/**
 * fscktape(machinenum, cachedir, prune, whichkey):
 * Correct any inconsistencies in the archive set (by removing orphaned or
 * corrupt files) and reconstruct the chunk directory in ${cachedir}.  If
 * ${prune} is zero, don't correct inconsistencies; instead, exit with an
 * error.  If ${whichkey} is zero, use the write key (for non-pruning fsck
 * only); otherwise, use the delete key.
 */
int
fscktape(uint64_t machinenum, const char * cachedir, int prune, int whichkey)
{
	STORAGE_D * SD;
	STORAGE_R * SR;
	CHUNKS_S * C;
	int lockfd;
	uint8_t seqnum[32];
	struct tapemetadata ** mdatlist;
	size_t nmdat;
	size_t file;
	uint8_t key = (whichkey == 0) ? 0 : 1;

	/* Lock the cache directory. */
	if ((lockfd = multitape_lock(cachedir)) == -1)
		goto err0;

	/* Make sure the lower layers are in a clean state. */
	if (multitape_cleanstate(cachedir, machinenum, key))
		goto err1;

	/*
	 * If a checkpointed archive creation was in progress on a different
	 * machine, we might as well commit it -- we're going to regenerate
	 * all of our local state anyway.
	 */
	if (storage_transaction_commitfromcheckpoint(machinenum, key))
		goto err1;

	/* Start a storage-layer fsck transaction. */
	if ((SD = storage_fsck_start(machinenum, seqnum,
	    prune ? 0 : 1, key)) == NULL)
		goto err1;

	/* Obtain a storage-layer read cookie. */
	if ((SR = storage_read_init(machinenum)) == NULL)
		goto err2;

	/*
	 * Phase 1: Read and parse all the metadata files, and delete any
	 * metadata files which are corrupt.
	 */
	if (phase1(machinenum, SD, SR, &mdatlist, &nmdat))
		goto err3;

	/*
	 * Phase 2: Verify that all the expected metaindex files exist; if
	 * any are missing, remove the associated metadata file, and if any
	 * extra metaindex files exist, remove them.
	 */
	if (phase2(machinenum, SD, mdatlist, nmdat))
		goto err4;

	/*
	 * Phase 3: Enumerate chunks.
	 */
	if ((C = phase3(machinenum, cachedir)) == NULL)
		goto err4;

	/*
	 * Phase 4: Make sure that all the chunks needed for the archives
	 * are in fact present; and in the process, reconstruct the chunk
	 * directory and extra statistics.
	 */
	if (phase4(SD, SR, C, mdatlist, nmdat))
		goto err5;

	/*
	 * Phase 5: Delete any unreferenced chunks.
	 */
	if (phase5(SD, C))
		goto err5;

	/* Free metadata structures. */
	for (file = 0; file < nmdat; file++) {
		multitape_metadata_free(mdatlist[file]);
		free(mdatlist[file]);
	}
	free(mdatlist);

	/* Finish the chunk layer fsck operation. */
	if (chunks_fsck_end(C))
		goto err3;

	/* Free the storage-layer read cookie. */
	storage_read_free(SR);

	/* Finish the storage layer fsck transaction. */
	if (storage_delete_end(SD))
		goto err1;

	/* Commit the transaction. */
	if (multitape_commit(cachedir, machinenum, seqnum, key))
		goto err1;

	/* Unlock the cache directory. */
	close(lockfd);

	/* Success! */
	return (0);

err5:
	chunks_stats_free(C);
err4:
	for (file = 0; file < nmdat; file++) {
		multitape_metadata_free(mdatlist[file]);
		free(mdatlist[file]);
	}
	free(mdatlist);
err3:
	storage_read_free(SR);
err2:
	storage_delete_free(SD);
err1:
	close(lockfd);
err0:
	/* Failure! */
	return (-1);
}

/**
 * statstape_initialize(machinenum, cachedir):
 * Initialize an empty chunk directory in ${cachedir} so that --print-stats
 * works.  This requires the "directory" file, but no other files.  Return 0
 * on success, -1 on error, and 1 if the cachedir is already initialized.
 */
int
statstape_initialize(uint64_t machinenum, const char * cachedir)
{
	int rc;

	(void) machinenum; /* UNUSED */

	/* Initialize the "directory" file. */
	if ((rc = chunks_initialize(cachedir)) != 0)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (rc);
}
