/* Native observations for issue831 / PR832. No server or real cache is used.
 * Compile the complete production reader and real Patricia implementation.
 * Allocation tracking and optional insertion failure affect only the reader.
 */
#include "platform.h"
#ifdef TEST_NO_MMAP
#undef HAVE_MMAP
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ccache_internal.h"
#include "multitape_internal.h"
#include "patricia.h"

#ifndef CCACHE_SOURCE
#error CCACHE_SOURCE must name the exact production ccache_read.c
#endif

struct allocation {
	void * ptr;
	size_t size;
};
static struct allocation allocations[64];
static size_t malloc_calls, realloc_calls, insert_calls, zero_reads;
static size_t open_files, fail_malloc, fail_realloc, fail_insert;
static size_t records, chunks, trailer_bytes, plain_bytes;
static unsigned long long inode_sum, payload_sum;
static int zero_malloc_null, insert_injected;

static int
slot_of(void * ptr)
{
	size_t i;
	if (ptr == NULL)
		return (-1);
	for (i = 0; i < 64; i++) {
		if (allocations[i].ptr == ptr)
			return ((int)i);
	}
	return (-1);
}

static void
remember(void * ptr, size_t size)
{
	size_t i;
	if (ptr == NULL)
		return;
	for (i = 0; i < 64; i++) {
		if (allocations[i].ptr == NULL) {
			allocations[i].ptr = ptr;
			allocations[i].size = size;
			return;
		}
	}
	fprintf(stderr, "fixture allocation table exhausted\n");
	exit(70);
}

static void *
fixture_malloc(size_t size)
{
	void * ptr;
	malloc_calls++;
	if ((malloc_calls == fail_malloc) || (size == 0 && zero_malloc_null)) {
		errno = ENOMEM;
		return (NULL);
	}
	ptr = malloc(size);
	remember(ptr, size);
	return (ptr);
}

static void *
fixture_realloc(void * old, size_t size)
{
	int slot = slot_of(old);
	void * ptr;
	realloc_calls++;
	if (realloc_calls == fail_realloc) {
		errno = ENOMEM;
		return (NULL);
	}
	ptr = realloc(old, size);
	if (ptr != NULL) {
		if (slot >= 0) {
			allocations[slot].ptr = ptr;
			allocations[slot].size = size;
		} else {
			remember(ptr, size);
		}
	}
	return (ptr);
}

static void
fixture_free(void * ptr)
{
	int slot = slot_of(ptr);
	if (slot >= 0) {
		allocations[slot].ptr = NULL;
		allocations[slot].size = 0;
	}
	free(ptr);
}

static size_t
fixture_fread(void * ptr, size_t size, size_t count, FILE * stream)
{
	if (size == 0 || count == 0)
		zero_reads++;
	return (fread(ptr, size, count, stream));
}

static FILE *
fixture_fopen(const char * path, const char * mode)
{
	FILE * stream = fopen(path, mode);
	if (stream != NULL)
		open_files++;
	return (stream);
}

static int
fixture_fclose(FILE * stream)
{
	int result = fclose(stream);
	open_files--;
	return (result);
}

static int
fixture_insert(PATRICIA * tree, const uint8_t * key, size_t size, void * rec)
{
	insert_calls++;
	if (insert_calls == fail_insert) {
		insert_injected = 1;
		errno = ENOMEM;
		return (-1);
	}
	return (patricia_insert(tree, key, size, rec));
}

#define malloc fixture_malloc
#define realloc fixture_realloc
#define free fixture_free
#define fread fixture_fread
#define fopen fixture_fopen
#define fclose fixture_fclose
#define patricia_insert fixture_insert
#include CCACHE_SOURCE
#undef malloc
#undef realloc
#undef free
#undef fread
#undef fopen
#undef fclose
#undef patricia_insert

static int
observe_record(void * cookie, uint8_t * key, size_t keylen, void * rec)
{
	struct ccache_record * record = rec;
	size_t i;
	(void)cookie;
	(void)key;
	(void)keylen;
	records++;
	chunks += record->nch;
	trailer_bytes += record->tzlen;
	plain_bytes += record->tlen;
	inode_sum += (unsigned long long)record->ino;
	for (i = 0; i < record->tzlen; i++)
		payload_sum += record->ztrailer[i];
	return (0);
}

int
main(int argc, char ** argv)
{
	CCACHE * cache;
	size_t i, live = 0, live_bytes = 0;
	int succeeded, visited = 0;
#ifdef HAVE_MMAP
	const char * mode = "mmap";
#else
	const char * mode = "read";
#endif

	if (argc == 2 && strcmp(argv[1], "--layout") == 0) {
		printf("{\"mode\":\"%s\",\"chunkheader_size\":%zu,"
		    "\"record_size\":%zu}\n", mode,
		    sizeof(struct chunkheader), sizeof(struct ccache_record));
		return (0);
	}
	if (argc != 6)
		return (64);
	fail_insert = (size_t)strtoul(argv[2], NULL, 10);
	zero_malloc_null = atoi(argv[3]);
	fail_malloc = (size_t)strtoul(argv[4], NULL, 10);
	fail_realloc = (size_t)strtoul(argv[5], NULL, 10);

	cache = ccache_read(argv[1]);
	succeeded = (cache != NULL);
	if (cache != NULL)
		visited = patricia_foreach(cache->tree, observe_record, NULL);
	ccache_free(cache);
	for (i = 0; i < 64; i++) {
		if (allocations[i].ptr != NULL) {
			live++;
			live_bytes += allocations[i].size;
		}
	}
	printf("{\"mode\":\"%s\",\"success\":%s,\"visit_result\":%d,"
	    "\"records\":%zu,\"chunks\":%zu,\"trailer_bytes\":%zu,"
	    "\"plain_bytes\":%zu,\"inode_sum\":%llu,\"payload_sum\":%llu,"
	    "\"live_allocations\":%zu,\"live_bytes\":%zu,\"open_files\":%zu,"
	    "\"malloc_calls\":%zu,\"realloc_calls\":%zu,\"insert_calls\":%zu,"
	    "\"insert_injected\":%s,\"zero_reads\":%zu}\n",
	    mode, succeeded ? "true" : "false", visited, records, chunks,
	    trailer_bytes, plain_bytes, inode_sum, payload_sum, live, live_bytes,
	    open_files, malloc_calls, realloc_calls, insert_calls,
	    insert_injected ? "true" : "false", zero_reads);

	/* Record leaks above, then clean only fixture-owned leftovers. This makes
	 * baseline failures repeatable; it does not turn them into passing tests. */
	for (i = 0; i < 64; i++)
		free(allocations[i].ptr);
	return (0);
}
