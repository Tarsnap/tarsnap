/* PR838 regression: compile the real metadata parser/get/free implementation.
 * Storage, signing, hash failures, and allocation failure are local fixtures.
 * This does not contact a server or test cryptographic verification.
 */
#include "platform.h"
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void * tracked_malloc(size_t);
static char * tracked_strdup(const char *);
static void tracked_free(void *);
#define malloc tracked_malloc
#define strdup tracked_strdup
#define free tracked_free
#ifndef METADATA_SOURCE
#define METADATA_SOURCE "../../../tar/multitape/multitape_metadata.c"
#endif
#include METADATA_SOURCE
#undef malloc
#undef strdup
#undef free

struct allocation { void * pointer; size_t length; };
static struct allocation allocations[64];
static size_t live_count, live_bytes, invalid_frees, allocation_calls;
static size_t fail_allocation;
static int read_status, signature_status, hash_calls, fail_hash_call;
static int name_mismatch, by_name, read_calls, signature_calls, warning_calls;
static int stats_calls;
static size_t stats_bytes;
static uint8_t fixture[4096];
static size_t fixture_length;
static const char * arguments[] = { "tarsnap", "-c", "folder" };

static void *
tracked_malloc(size_t size)
{
	void * pointer;
	size_t i;
	allocation_calls++;
	if (allocation_calls == fail_allocation) {
		errno = ENOMEM;
		return (NULL);
	}
	if ((pointer = malloc(size ? size : 1)) == NULL)
		abort();
	for (i = 0; i < 64; i++) {
		if (allocations[i].pointer == NULL) {
			allocations[i].pointer = pointer;
			allocations[i].length = size;
			live_count++;
			live_bytes += size;
			return (pointer);
		}
	}
	abort();
}

static char *
tracked_strdup(const char * text)
{
	size_t length = strlen(text) + 1;
	char * pointer = tracked_malloc(length);
	if (pointer != NULL)
		memcpy(pointer, text, length);
	return (pointer);
}

static void
tracked_free(void * pointer)
{
	size_t i;
	if (pointer == NULL)
		return;
	for (i = 0; i < 64; i++) {
		if (allocations[i].pointer == pointer) {
			live_bytes -= allocations[i].length;
			live_count--;
			allocations[i].pointer = NULL;
			free(pointer);
			return;
		}
	}
	invalid_frees++;
}

int
storage_read_file_alloc(STORAGE_R * storage, uint8_t ** data, size_t * length,
    char class, const uint8_t hash[32])
{
	(void)storage;
	(void)hash;
	if (class != 'm')
		abort();
	read_calls++;
	if (read_status != 0)
		return (read_status);
	if ((*data = tracked_malloc(fixture_length)) == NULL)
		return (-1);
	memcpy(*data, fixture, fixture_length);
	*length = fixture_length;
	return (0);
}

int
crypto_hash_data(int key, const uint8_t * data, size_t length, uint8_t hash[32])
{
	if (key != CRYPTO_KEY_HMAC_NAME || length != strlen("archive") ||
	    memcmp(data, "archive", length) != 0)
		abort();
	hash_calls++;
	if (hash_calls == fail_hash_call)
		return (-1);
	memset(hash, name_mismatch && hash_calls == (by_name ? 2 : 1) ? 0xa5 : 0x5a, 32);
	return (0);
}

int
crypto_rsa_verify(int key, const uint8_t * data, size_t length,
    const uint8_t * signature, size_t signature_length)
{
	(void)data;
	(void)length;
	(void)signature;
	if (key != CRYPTO_KEY_SIGN_PUB || signature_length != 256)
		abort();
	signature_calls++;
	return (signature_status);
}

void
chunks_stats_extrastats(CHUNKS_S * chunks, size_t length)
{
	(void)chunks;
	stats_calls++;
	stats_bytes += length;
}

void
libcperciva_warn(const char * format, ...)
{
	(void)format;
	warning_calls++;
}

void
libcperciva_warnx(const char * format, ...)
{
	(void)format;
	warning_calls++;
}

static void
make_fixture(int argc)
{
	uint8_t * p = fixture;
	int i;
	memcpy(p, "archive", 8); p += 8;
	le64enc(p, 1700000000); p += 8;
	le32enc(p, (uint32_t)argc); p += 4;
	for (i = 0; i < argc; i++) {
		memcpy(p, arguments[i], strlen(arguments[i]) + 1);
		p += strlen(arguments[i]) + 1;
	}
	memset(p, 0x3c, 32); p += 32;
	le64enc(p, 12345); p += 8;
	memset(p, 0x6d, 256); p += 256;
	fixture_length = (size_t)(p - fixture);
}

int
main(int argc, char ** argv)
{
	struct tapemetadata metadata;
	uint8_t requested[32];
	const char * scenario;
	int count, quiet, expected = 0, actual = 0, passed = 1, repetitions = 1, i;
	size_t leaked_count, leaked_bytes;
	if (argc != 5)
		return (2);
	scenario = argv[1]; count = atoi(argv[2]); quiet = atoi(argv[3]); by_name = atoi(argv[4]);
	if ((count != 0 && count != 1 && count != 3) || quiet < 0 || quiet > 1 || by_name < 0 || by_name > 1)
		return (2);
	make_fixture(count);
	memset(requested, 0x5a, 32);
	if (strcmp(scenario, "mismatch") == 0 || strcmp(scenario, "repeat-mismatch") == 0) {
		name_mismatch = 1; expected = 2;
		if (strcmp(scenario, "repeat-mismatch") == 0)
			repetitions = 100;
	} else if (strcmp(scenario, "hash-error") == 0) {
		fail_hash_call = by_name ? 2 : 1; expected = -1;
	} else if (strcmp(scenario, "initial-hash-error") == 0) {
		if (!by_name) return (2);
		fail_hash_call = 1; expected = -1;
	} else if (strcmp(scenario, "missing") == 0) {
		read_status = 1; expected = 1;
	} else if (strcmp(scenario, "read-corrupt") == 0) {
		read_status = 2; expected = 2;
	} else if (strcmp(scenario, "read-error") == 0) {
		read_status = -1; expected = -1;
	} else if (strcmp(scenario, "bad-signature") == 0) {
		signature_status = 1; expected = 2;
	} else if (strcmp(scenario, "signature-error") == 0) {
		signature_status = -1; expected = -1;
	} else if (strcmp(scenario, "short-signature") == 0) {
		fixture_length--; expected = 2;
	} else if (strcmp(scenario, "unterminated-name") == 0) {
		memset(fixture, 'x', fixture_length); expected = 2;
	} else if (strcmp(scenario, "truncated-argv") == 0) {
		fixture_length = 24; memset(fixture + 20, 'x', 4); expected = 2;
	} else if (strcmp(scenario, "negative-argc") == 0) {
		le32enc(fixture + 16, UINT32_MAX); expected = 2;
	} else if (strcmp(scenario, "trailing-byte") == 0) {
		fixture[fixture_length++] = 1; expected = 2;
	} else if (strncmp(scenario, "allocation-", 11) == 0) {
		fail_allocation = (size_t)atoi(scenario + 11); expected = -1;
		if (fail_allocation < 1 || fail_allocation > (size_t)count + 3) return (2);
	} else if (strcmp(scenario, "success") != 0) {
		return (2);
	}
	for (i = 0; i < repetitions; i++) {
		memset(&metadata, 0xa5, sizeof(metadata));
		hash_calls = 0;
		actual = by_name ? multitape_metadata_get_byname(NULL, (CHUNKS_S *)&stats_calls,
		    &metadata, "archive", quiet) : multitape_metadata_get_byhash(NULL,
		    (CHUNKS_S *)&stats_calls, &metadata, requested, quiet);
		if (actual != expected) passed = 0;
		if (actual == 0) {
			/* Success transfers the allocation ownership to the caller. */
			if (live_count != (size_t)count + 2) passed = 0;
			else if (strcmp(metadata.name, "archive") != 0 || metadata.argc != count ||
			    metadata.indexlen != 12345 || metadata.metadatalen != fixture_length)
				passed = 0;
			multitape_metadata_free(&metadata);
		}
		if (live_count || invalid_frees) { passed = 0; break; }
	}
	if ((name_mismatch || strcmp(scenario, "hash-error") == 0 || expected == 0) && signature_calls != repetitions)
		passed = 0;
	if (strcmp(scenario, "initial-hash-error") == 0 && read_calls != 0) passed = 0;
	if (name_mismatch && warning_calls != (quiet ? 0 : repetitions)) passed = 0;
	if (stats_calls && stats_bytes != (size_t)stats_calls * fixture_length) passed = 0;
	leaked_count = live_count; leaked_bytes = live_bytes;
	/* Clean fixture leftovers AFTER recording failure; no leaks are concealed. */
	for (i = 0; i < 64; i++) if (allocations[i].pointer != NULL) tracked_free(allocations[i].pointer);
	printf("{\"scenario\":\"%s\",\"argc\":%d,\"quiet\":%d,\"by_name\":%d,\"passed\":%s,\"actual\":%d,\"expected\":%d,\"live_allocations\":%zu,\"live_bytes\":%zu,\"invalid_frees\":%zu,\"signature_calls\":%d,\"read_calls\":%d}\n",
	    scenario, count, quiet, by_name, passed ? "true" : "false", actual, expected,
	    leaked_count, leaked_bytes, invalid_frees, signature_calls, read_calls);
	return (passed ? 0 : 1);
}
