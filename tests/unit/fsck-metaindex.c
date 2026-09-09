/* Real fsck phase2 with deterministic in-memory storage/metadata fixtures. */
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocations.h"

#define malloc unit_malloc
#define free unit_free
#define memset unit_memset
#include "../../tar/multitape/multitape_fsck.c"
#undef memset
#undef free
#undef malloc
#include "../../libcperciva/util/hexify.c"

static size_t count;
static int read_fail, delete_fail, deletes, scenarios;
static int metadata_frees;

int
storage_directory_read(uint64_t machine, char class, int key,
    uint8_t ** files, size_t * nfiles)
{
	size_t i;

	assert(machine == 17 && class == 'i' && key == 0);
	if (read_fail)
		return (-1);
	*nfiles = count;
	*files = count == 0 ? NULL : unit_malloc(32 * count);
	assert(count == 0 || *files != NULL);
	for (i = 0; i < count; i++) {
		memset(*files + 32 * i, 1, 32);
		(*files)[32 * i + 31] = (uint8_t)i;
	}
	return (0);
}

int
crypto_hash_data(int key, const uint8_t * input, size_t len, uint8_t out[32])
{
	assert(key == CRYPTO_KEY_HMAC_NAME && input != NULL && len == 7);
	memset(out, 1, 32);
	out[31] = 0;
	return (0);
}

void
multitape_metaindex_fragname(const uint8_t hash[32], uint32_t part,
    uint8_t out[32])
{
	assert(part < 2);
	memcpy(out, hash, 32);
	out[31] = (uint8_t)part;
}

int
storage_delete_file(STORAGE_D * storage, char class, const uint8_t hash[32])
{
	(void)storage;
	assert(class == 'i' || class == 'm');
	assert(hash[0] == 1 && hash[30] == 1);
	deletes++;
	return (delete_fail ? -1 : 0);
}

void
multitape_metadata_free(struct tapemetadata * m)
{
	unit_free(m->name);
	metadata_frees++;
}

static void
scenario(size_t nfiles, int has_metadata, int failure)
{
	struct tapemetadata * metadata = NULL;
	struct tapemetadata * list[1];
	int rc, expected_deletes;

	unit_reset(0);
	if (has_metadata) {
		assert((metadata = unit_malloc(sizeof(*metadata))) != NULL);
		memset(metadata, 0, sizeof(*metadata));
		assert((metadata->name = unit_strdup("archive")) != NULL);
		metadata->indexlen = 2 * MAXIFRAG;
	}
	list[0] = metadata;
	count = nfiles;
	read_fail = failure == 1;
	delete_fail = failure == 3;
	deletes = metadata_frees = 0;
	/* The only allocation in phase2 after the listing is neededvec. */
	if (failure == 2)
		unit_fail_at = unit_requests + 2;
	rc = phase2(17, NULL, list, (size_t)has_metadata);
	assert(rc == (failure == 0 ? 0 : -1));
	if (failure == 0) {
		expected_deletes = !has_metadata ? (int)nfiles :
		    (nfiles < 2 ? (int)nfiles + 1 : (int)nfiles - 2);
		assert(deletes == expected_deletes);
		assert(metadata_frees == (has_metadata && nfiles < 2 ? 1 : 0));
	}
	if (list[0] != NULL) {
		multitape_metadata_free(list[0]);
		unit_free(list[0]);
	}
	assert(unit_live == 0);
	scenarios++;
}

int
main(void)
{
	size_t n;
	int metadata;

	for (metadata = 0; metadata <= 1; metadata++) {
		for (n = 0; n <= 4; n++) {
			scenario(n, metadata, 0);
			scenario(n, metadata, 1);
			if (n > 0)
				scenario(n, metadata, 2);
			if ((!metadata && n > 0) ||
			    (metadata && n != 2))
				scenario(n, metadata, 3);
		}
	}
	printf("PASS: %d fsck empty-list, retention and cleanup scenarios\n",
	    scenarios);
	return (0);
}
