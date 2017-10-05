#include "bsdtar_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "storage.h"

#include "multitape.h"

/**
 * nuketape(machinenum):
 * Delete all files in the archive set.
 */
int
nuketape(uint64_t machinenum)
{
	STORAGE_D * SD;
	uint8_t seqnum[32];
	uint8_t * flist;
	size_t nfiles;
	size_t i;

	/* Start a storage-layer fsck transaction. */
	if ((SD = storage_fsck_start(machinenum, seqnum, 0, 1)) == NULL)
		goto err0;

	/* Obtain a list of metadata files. */
	if (storage_directory_read(machinenum, 'm', 1, &flist, &nfiles))
		goto err1;

	/* Iterate through the metadata files, deleting them. */
	for (i = 0; i < nfiles; i++) {
		if (storage_delete_file(SD, 'm', &flist[i * 32]))
			goto err2;
	}

	/* Free list of metadata files. */
	free(flist);

	/* Obtain a list of metaindex fragments. */
	if (storage_directory_read(machinenum, 'i', 1, &flist, &nfiles))
		goto err1;

	/* Iterate through the metaindex fragments, deleting them. */
	for (i = 0; i < nfiles; i++) {
		if (storage_delete_file(SD, 'i', &flist[i * 32]))
			goto err2;
	}

	/* Free list of metaindex fragments. */
	free(flist);

	/* Obtain a list of chunk files. */
	if (storage_directory_read(machinenum, 'c', 1, &flist, &nfiles))
		goto err1;

	/* Iterate through the chunk files, deleting them. */
	for (i = 0; i < nfiles; i++) {
		if (storage_delete_file(SD, 'c', &flist[i * 32]))
			goto err2;
	}

	/* Free list of chunk files. */
	free(flist);

	/* Finish the storage layer fsck transaction. */
	if (storage_delete_end(SD))
		goto err0;

	/*
	 * Bypass the normal multitape transaction commit code (which makes
	 * sure that the cache directory is in sync with the server) and
	 * ask the storage layer to commit the transaction.
	 */
	if (storage_transaction_commit(machinenum, seqnum, 1))
		goto err0;

	/* Success! */
	return (0);

err2:
	free(flist);
err1:
	storage_delete_free(SD);
err0:
	/* Failure! */
	return (-1);
}
