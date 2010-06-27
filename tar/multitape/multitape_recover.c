#include "bsdtar_platform.h"

#include <stdint.h>

#include "multitape_internal.h"

#include "multitape.h"

/**
 * recovertape(machinenum, cachedir, whichkey):
 * Complete any pending checkpoint or commit, including a checkpoint in a
 * write transaction being performed by a different machine (if any).  If
 * ${whichkey} is zero, use the write key; otherwise, use the delete key.
 */
int
recovertape(uint64_t machinenum, const char * cachedir, int whichkey)
{
	uint8_t key = (whichkey == 0) ? 0 : 1;

	/* Complete any pending checkpoints or commits locally. */
	if (multitape_cleanstate(cachedir, machinenum, key))
		goto err0;

	/* Complete any non-local pending checkpoint. */
	if (storage_transaction_commitfromcheckpoint(machinenum, key))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
