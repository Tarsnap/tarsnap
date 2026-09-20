/* Real files, decoder, hash table and statistics; zero-allocation is injected. */
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "allocations.h"

#define malloc unit_malloc
#define free unit_free
#include "../../tar/chunks/chunks_directory.c"
#undef free
#undef malloc
#include "../../tar/chunks/chunks_stats_internal.c"
#include "../../lib/datastruct/rwhashtab.c"
#include "../../libcperciva/util/asprintf.c"

static int scenarios;

int
crypto_entropy_read(uint8_t * buf, size_t len)
{
	memset(buf, 1, len);
	return (0);
}

int
crypto_hash_data_2(int key, const uint8_t * a, size_t alen,
    const uint8_t * b, size_t blen, uint8_t out[32])
{
	(void)key;
	assert(a != NULL && b != NULL && alen == 32 && blen == 32);
	/* Force collisions; the real hash table still compares full keys. */
	memset(out, 0, 32);
	return (0);
}

void
libcperciva_warn(const char * fmt, ...)
{
	assert(fmt != NULL);
}

void
libcperciva_warnx(const char * fmt, ...)
{
	assert(fmt != NULL);
}

static void
fixture(const char * path, size_t count, int corrupt)
{
	uint8_t header[24] = {0}, record[48];
	FILE * f;
	size_t i;

	assert((f = fopen(path, "wb")) != NULL);
	le64enc(header, 7);
	le64enc(header + 8, 11);
	le64enc(header + 16, 13);
	assert(fwrite(header, 1, corrupt == 1 ? 23 : 24, f) ==
	    (size_t)(corrupt == 1 ? 23 : 24));
	if (corrupt == 2)
		assert(fputc(0, f) == 0);
	for (i = 0; i < count; i++) {
		memset(record, (int)i + 1, 32);
		le32enc(record + 32, corrupt == 3 ? 0 : (uint32_t)(100 + i));
		le32enc(record + 36, (uint32_t)(50 + i));
		le32enc(record + 40, 1);
		le32enc(record + 44, 2);
		assert(fwrite(record, 1, sizeof(record), f) == sizeof(record));
	}
	assert(fclose(f) == 0);
}

static void
scenario(const char * directory, const char * path, size_t count,
    int statstape, int failure, int corrupt)
{
	struct chunkstats unique, all, extra;
	struct chunkdata * chunk;
	RWHASHTAB * table;
	void * dir = NULL;
	uint8_t key[32];
	size_t i;

	unit_reset(failure ? 1 : 0);
	fixture(path, count, corrupt);
	table = chunks_directory_read(directory, &dir, &unique, &all,
	    &extra, 1, statstape);
	assert((table == NULL) == (failure || corrupt));
	if (table != NULL) {
		assert((dir == NULL) == (count == 0));
		assert(rwhashtab_getsize(table) == count);
		assert(unique.nchunks == count && all.nchunks == 2 * count);
		assert(unique.s_len == count * 100 + count * (count - 1) / 2);
		assert(all.s_len == 2 * unique.s_len);
		assert(extra.nchunks == 7 && extra.s_len == 11 && extra.s_zlen == 13);
		for (i = 0; i < count; i++) {
			memset(key, (int)i + 1, sizeof(key));
			chunk = rwhashtab_read(table, key);
			assert(chunk != NULL && chunk->len == 100 + i);
			assert(chunk->zlen_flags == 50 + i);
			assert(chunk->nrefs == 1 && chunk->ncopies == 2);
		}
		chunks_directory_free(table, dir);
	}
	assert(unit_live == 0);
	scenarios++;
}

int
main(void)
{
	char directory[] = "/tmp/tarsnap-chunks-XXXXXX";
	char path[128];
	struct chunkstats unique, all, extra;
	RWHASHTAB * table;
	void * dir;
	size_t count;
	int statstape, mustexist;

	assert(mkdtemp(directory) != NULL);
	assert(snprintf(path, sizeof(path), "%s/directory", directory) > 0);
	for (statstape = 0; statstape <= 1; statstape++) {
		for (count = 0; count <= 4; count++) {
			scenario(directory, path, count, statstape, 0, 0);
			if (count > 0)
				scenario(directory, path, count, statstape, 1, 0);
		}
		scenario(directory, path, 0, statstape, 0, 1);
		scenario(directory, path, 0, statstape, 0, 2);
		scenario(directory, path, 1, statstape, 0, 3);
	}
	assert(unlink(path) == 0);
	for (mustexist = 0; mustexist <= 1; mustexist++) {
		unit_reset(0);
		dir = &count;
		table = chunks_directory_read(directory, &dir, &unique, &all,
		    &extra, mustexist, 0);
		assert((table == NULL) == mustexist);
		if (table != NULL) {
			assert(dir == NULL && rwhashtab_getsize(table) == 0);
			chunks_directory_free(table, dir);
		}
		assert(unit_live == 0);
		scenarios++;
	}
	unit_reset(0);
	table = chunks_directory_read(NULL, &dir, &unique, &all, &extra, 1, 1);
	assert(table != NULL && dir == NULL && unique.nchunks == 0);
	chunks_directory_free(table, dir);
	assert(unit_live == 0 && rmdir(directory) == 0);
	printf("PASS: %d real-file chunk-directory scenarios\n", ++scenarios);
	return (0);
}
