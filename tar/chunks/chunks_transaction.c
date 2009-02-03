#include "bsdtar_platform.h"

#include "chunks_internal.h"

#include "chunks.h"

/**
 * chunks_transaction_checkpoint(cachepath):
 * Mark the pending checkpoint in the cache directory ${cachepath} as being
 * ready to commit from the perspective of the chunk layer.
 */
int
chunks_transaction_checkpoint(const char * cachepath)
{

	if (chunks_directory_commit(cachepath, ".ckpt", ".tmp"))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_transaction_commit(cachepath):
 * Commit the last finished transaction in the cache directory ${cachepath}
 * from the perspective of the chunk layer.
 */
int
chunks_transaction_commit(const char * cachepath)
{

	/*
	 * The only data belonging to the chunk layer which is stored in the
	 * cache directory ${cachepath} is the chunk directory; so tell that
	 * code to handle the commit.
	 */
	if (chunks_directory_commit(cachepath, ".tmp", ""))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
