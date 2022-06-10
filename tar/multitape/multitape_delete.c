#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chunks.h"
#include "crypto.h"
#include "multitape_internal.h"
#include "storage.h"

#include "multitape.h"

/* Cookie created by deletetape_init and passed to other functions. */
struct multitape_delete_internal {
	STORAGE_R * S;	/* Storage read cookie. */
};

static int callback_delete(void *, struct chunkheader *);

/**
 * callback_delete(cookie, ch):
 * Call chunks_delete_chunk on the chunk delete cookie ${cookie} and the
 * chunk header ${ch}.
 */
static int
callback_delete(void * cookie, struct chunkheader * ch)
{
	CHUNKS_D * C = cookie;

	return (chunks_delete_chunk(C, ch->hash));
}

/**
 * deletetape_init(machinenum):
 * Return a cookie which can be passed to deletetape.
 */
TAPE_D *
deletetape_init(uint64_t machinenum)
{
	struct multitape_delete_internal * d;

	/* Allocate memory. */
	if ((d = malloc(sizeof(struct multitape_delete_internal))) == NULL)
		goto err0;

	/* Obtain a storage layer read cookie. */
	if ((d->S = storage_read_init(machinenum)) == NULL)
		goto err1;

	/* Success! */
	return (d);

err1:
	free(d);
err0:
	/* Failure!  */
	return (NULL);
}

/**
 * deletetape(d, machinenum, cachedir, tapename, printstats, withname,
 *     csv_filename):
 * Delete the specified tape, and print statistics to stderr if requested.
 * If ${withname} is non-zero, print statistics with the archive name, not
 * just as "This archive".  Return 0 on success, 1 if the tape does not exist,
 * or -1 on other errors.  If ${csv_filename} is specified, output in CSV
 * format instead of to stderr.
 */
int
deletetape(TAPE_D * d, uint64_t machinenum, const char * cachedir,
    const char * tapename, int printstats, int withname,
    const char * csv_filename)
{
	struct tapemetadata tmd;
	CHUNKS_D * C;		/* Chunk layer delete cookie. */
	STORAGE_D * S;		/* Storage layer delete cookie. */
	STORAGE_R * SR = d->S;	/* Storage layer read cookie. */
	int lockfd;
	uint8_t lastseq[32];
	uint8_t seqnum[32];
	int rc = -1;		/* Presume error was not !found. */
	FILE * output = stderr;
	int csv = 0;

	/* Should we output to a CSV file? */
	if (csv_filename != NULL)
		csv = 1;

	/* Lock the cache directory. */
	if ((lockfd = multitape_lock(cachedir)) == -1)
		goto err0;

	/* Make sure the lower layers are in a clean state. */
	if (multitape_cleanstate(cachedir, machinenum, 1))
		goto err1;

	/* Get sequence number (# of last committed transaction). */
	if (multitape_sequence(cachedir, lastseq))
		goto err1;

	/* Obtain storage and chunk layer cookies. */
	if ((S = storage_delete_start(machinenum, lastseq, seqnum)) == NULL)
		goto err1;
	if ((C = chunks_delete_start(cachedir, S)) == NULL)
		goto err2;

	/* Cache up to 100 bytes of blocks per chunk in the directory. */
	storage_read_set_cache_limit(SR, 100 * chunks_delete_getdirsz(C));

	/* Read archive metadata. */
	switch (multitape_metadata_get_byname(SR, NULL, &tmd, tapename, 0)) {
	case 0:
		break;
	case 1:
		rc = 1;
		/* FALLTHROUGH */
	default:
		goto err3;
	}

	/* Delete chunks. */
	if (multitape_chunkiter_tmd(SR, NULL, &tmd, callback_delete, C, 0))
		goto err5;

	/* Delete archive index. */
	if (multitape_metaindex_delete(S, C, &tmd))
		goto err5;

	/* Delete archive metadata. */
	if (multitape_metadata_delete(S, C, &tmd))
		goto err5;

	/* Free tape metadata. */
	multitape_metadata_free(&tmd);

	/* Ask the storage layer to flush all pending deletes. */
	if (storage_delete_flush(S))
		goto err3;

	/* Print statistics if they were requested. */
	if (printstats != 0) {
		if (csv && (output = fopen(csv_filename, "w")) == NULL)
			goto err3;

		/* Actually print statistics. */
		if (chunks_delete_printstats(output, C,
		    withname ? tapename : NULL, csv))
			goto err4;

		if (csv && fclose(output))
			goto err3;
	}

	/* Close storage and chunk layer cookies. */
	if (chunks_delete_end(C))
		goto err2;
	if (storage_delete_end(S))
		goto err1;

	/* Commit the transaction. */
	if (multitape_commit(cachedir, machinenum, seqnum, 1))
		goto err1;

	/* Unlock the cache directory. */
	close(lockfd);

	/* Success! */
	return (0);

err5:
	multitape_metadata_free(&tmd);
err4:
	if (output != stderr)
		fclose(output);
err3:
	chunks_delete_free(C);
err2:
	storage_delete_free(S);
err1:
	close(lockfd);
err0:
	/* Failure! */
	return (rc);
}

/**
 * deletetape_free(d):
 * Free the delete cookie ${d}.
 */
void
deletetape_free(TAPE_D * d)
{

	/* Behave consistently with free(NULL). */
	if (d == NULL)
		return;

	/* Close the storage layer read cookie. */
	storage_read_free(d->S);

	/* Free the multitape layer delete cookie. */
	free(d);
}
