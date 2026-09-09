/* Actual initialization/error ladders with tracked allocation ownership. */
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocations.h"

#define malloc unit_malloc
#define strdup unit_strdup
#define free unit_free
#include "../../tar/chunks/chunks_stats.c"
#undef free
#undef strdup
#undef malloc
#include "../../tar/chunks/chunks_stats_internal.c"

static size_t count, inserted;
static int mode, scenarios;

int
storage_directory_read(uint64_t machine, char class, int key,
    uint8_t ** files, size_t * nfiles)
{
	size_t i;

	assert(machine == 17 && class == 'c' && key == 0);
	if (mode == 3)
		return (-1);
	*nfiles = count;
	*files = count == 0 ? NULL : unit_malloc(count * 32);
	assert(count == 0 || *files != NULL);
	for (i = 0; i < count; i++)
		memset(*files + i * 32, (int)(i + 1), 32);
	return (0);
}

RWHASHTAB *
rwhashtab_init(size_t offset, size_t length)
{
	assert(offset == offsetof(struct chunkdata, hash) && length == 32);
	return (mode == 5 ? NULL : unit_malloc(1));
}

int
rwhashtab_insert(RWHASHTAB * table, void * record)
{
	struct chunkdata_statstape * c = record;

	assert(table != NULL && record != NULL);
	assert(c->d.hash[0] == inserted + 1);
	assert(c->d.hash[31] == inserted + 1);
	assert(c->d.len == 0 && c->d.zlen_flags == 0);
	assert(c->d.nrefs == 0 && c->d.ncopies == 0);
	inserted++;
	if ((mode == 6 && inserted == 1) ||
	    (mode == 7 && inserted == 2))
		return (-1);
	return (0);
}

void
rwhashtab_free(RWHASHTAB * table)
{
	unit_free(table);
}

void
chunks_directory_free(RWHASHTAB * table, void * dir)
{
	rwhashtab_free(table);
	unit_free(dir);
}

static void
scenario(size_t nfiles, int failure)
{
	CHUNKS_S * c;
	size_t fail_at = failure == 1 ? 1 : (failure == 2 ? 2 : 0);

	if (failure == 4)
		fail_at = 4;
	unit_reset(fail_at);
	count = nfiles;
	mode = failure;
	inserted = 0;
	c = chunks_fsck_start(17, "fixture-cache");
	assert((c == NULL) == (failure != 0));
	if (c != NULL) {
		assert(strcmp(c->cachepath, "fixture-cache") == 0);
		assert((c->dir == NULL) == (nfiles == 0));
		assert(inserted == nfiles);
		assert(c->stats_total.nchunks == 0);
		assert(c->stats_unique.s_len == 0 && c->stats_extra.s_zlen == 0);
		chunks_stats_free(c);
	}
	/* Includes the file list, directory, cookie, path and fake table. */
	assert(unit_live == 0);
	scenarios++;
}

int
main(void)
{
	size_t n;
	int failure;

	for (n = 0; n <= 4; n++) {
		scenario(n, 0);
		for (failure = 1; failure <= 3; failure++)
			scenario(n, failure);
		if (n != 0) {
			for (failure = 4; failure <= 6; failure++)
				scenario(n, failure);
		}
		if (n >= 2)
			scenario(n, 7);
	}
	chunks_stats_free(NULL);
	printf("PASS: %d empty-list and allocation-cleanup scenarios\n", scenarios);
	return (0);
}
