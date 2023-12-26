#include "platform.h"

#include <stdint.h>

#include "multitape_internal.h"
#include "storage.h"

#include "multitape.h"

/**
 * recovertape(machinenum, cachedir, whichkey, storage_modified):
 * Complete any pending checkpoint or commit, including a checkpoint in a
 * write transaction being performed by a different machine (if any).  If
 * ${whichkey} is zero, use the write key; otherwise, use the delete key.
 * If the data on the server has been modified, set ${*storage_modified} to 1.
 */
int
recovertape(uint64_t machinenum, const char * cachedir, int whichkey,
    int * storage_modified)
{
	uint8_t key = (whichkey == 0) ? 0 : 1;

	/* Complete any pending checkpoints or commits locally. */
	if (multitape_cleanstate(cachedir, machinenum, key, storage_modified))
		goto err0;

	/* Complete any non-local pending checkpoint. */
	if (storage_transaction_commitfromcheckpoint(machinenum, key,
	    storage_modified))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
