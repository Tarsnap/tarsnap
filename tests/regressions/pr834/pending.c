/* Exercise actual storage deletion ownership with local, bounded event fixtures.
 * No request leaves this process. The netpacket/event service is synthetic.
 */
#include "platform.h"
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void * tracked_malloc(size_t);
static void tracked_free(void *);
#define malloc tracked_malloc
#define free tracked_free
#ifndef DELETE_SOURCE
#define DELETE_SOURCE "../../../tar/storage/storage_delete.c"
#endif
#include DELETE_SOURCE
#undef malloc
#undef free

static void * allocations[4096];
static size_t live_allocations, invalid_frees;
static int fail_allocation, reject_op;
static struct delete_file_internal * queue[2048];
static size_t queued;
static int select_calls, completed, empty_selects, close_calls, op_calls;
static int fail_after_completions = -1;
static handlepacket_callback * response_callback;
static uint8_t file_name[32];

static void *
tracked_malloc(size_t size)
{
	void * p;
	size_t i;
	if (fail_allocation) {
		fail_allocation = 0;
		errno = ENOMEM;
		return (NULL);
	}
	if ((p = malloc(size)) == NULL) abort();
	for (i = 0; i < 4096; i++) {
		if (allocations[i] == NULL) {
			allocations[i] = p;
			live_allocations++;
			return (p);
		}
	}
	abort();
}

static void
tracked_free(void * p)
{
	size_t i;
	if (p == NULL) return;
	for (i = 0; i < 4096; i++) {
		if (allocations[i] == p) {
			allocations[i] = NULL;
			live_allocations--;
			free(p);
			return;
		}
	}
	invalid_frees++;
}

int
netpacket_delete_file(NETPACKET_CONNECTION * npc, uint64_t machine,
    uint8_t class, const uint8_t name[32], const uint8_t nonce[32],
    handlepacket_callback * callback)
{
	(void)npc;
	if (machine != 123 || class != 'c' || memcmp(name, file_name, 32) ||
	    nonce[0] != 0x39) abort();
	response_callback = callback;
	return (0);
}

int
netpacket_op(NETPACKET_CONNECTION * npc, sendpacket_callback * send, void * cookie)
{
	op_calls++;
	/* Model rejection before the operation is accepted or callback is invoked. */
	if (reject_op) return (-1);
	if (queued >= 2048 || send(cookie, npc) != 0) abort();
	queue[queued++] = cookie;
	return (0);
}

int
netpacket_hmac_verify(uint8_t type, const uint8_t nonce[32],
    const uint8_t * data, size_t length, int key)
{
	(void)nonce;
	(void)data;
	if (type != NETPACKET_DELETE_FILE_RESPONSE || length != 34 ||
	    key != CRYPTO_KEY_AUTH_DELETE) abort();
	return (0);
}

int
network_select(int blocking)
{
	struct delete_file_internal * cookie;
	uint8_t response[66] = {0};
	if (blocking != 1) abort();
	select_calls++;
	if (fail_after_completions >= 0 && completed >= fail_after_completions)
		return (-1);
	if (queued == 0) {
		/* Bound a phantom wait: two empty iterations, then fixture error. */
		empty_selects++;
		return (empty_selects >= 3 ? -1 : 0);
	}
	cookie = queue[0];
	memmove(queue, queue + 1, (--queued) * sizeof(queue[0]));
	response[1] = cookie->class;
	memcpy(response + 2, cookie->name, 32);
	completed++;
	return (response_callback(cookie, NULL, NETWORK_STATUS_OK,
	    NETPACKET_DELETE_FILE_RESPONSE, response, sizeof(response)));
}

int
netpacket_close(NETPACKET_CONNECTION * npc)
{
	(void)npc;
	close_calls++;
	return (0);
}

void libcperciva_warn(const char * format, ...) { (void)format; }
void libcperciva_warnx(const char * format, ...) { (void)format; }
void netproto_printerr_internal(int status) { (void)status; }

int
main(int argc, char ** argv)
{
	STORAGE_D * storage;
	const char * scenario;
	int initial, parameter, i, actual = 0, expected, passed = 1;
	int flush_result, end_result, flush_calls, flush_empty;
	size_t after_pending, after_queued, after_allocations, leftovers;
	if (argc != 4) return (2);
	scenario = argv[1]; initial = atoi(argv[2]); parameter = atoi(argv[3]);
	if (initial < 0 || initial > 1024 || parameter < 0 || parameter > 1000)
		return (2);
	if ((storage = tracked_malloc(sizeof(*storage))) == NULL) abort();
	memset(storage, 0, sizeof(*storage));
	storage->machinenum = 123;
	memset(storage->nonce, 0x39, 32);
	memset(file_name, 0x7b, 32);
	for (i = 0; i < initial; i++)
		if (storage_delete_file(storage, 'c', file_name) != 0) abort();
	if (storage->npending != queued || live_allocations != queued + 1) abort();
	if (!strcmp(scenario, "readonly")) storage->readonly = 1;
	else if (!strcmp(scenario, "allocation-failure")) fail_allocation = 1;
	else if (!strcmp(scenario, "issue-reject") || !strcmp(scenario, "repeat-reject") ||
	    !strcmp(scenario, "success-after-reject")) reject_op = 1;
	else if (!strcmp(scenario, "throttle-error")) {
		if (initial != 1024) return (2);
		fail_after_completions = parameter;
	} else if (strcmp(scenario, "success")) return (2);
	expected = !strcmp(scenario, "success") ? 0 : -1;
	for (i = 0; i < (!strcmp(scenario, "repeat-reject") ? parameter : 1); i++) {
		actual = storage_delete_file(storage, 'c', file_name);
		if (actual != expected || storage->npending != queued ||
		    live_allocations != queued + 1 || invalid_frees) passed = 0;
		if (!passed) break;
	}
	after_pending = storage->npending;
	after_queued = queued;
	after_allocations = live_allocations;
	if (!strcmp(scenario, "readonly") || !strcmp(scenario, "allocation-failure")) {
		if (op_calls != initial || select_calls != 0) passed = 0;
	}
	if (!strcmp(scenario, "throttle-error")) {
		if (completed != parameter || op_calls != initial) passed = 0;
	}
	reject_op = 0; fail_after_completions = -1; storage->readonly = 0;
	if (!strcmp(scenario, "success-after-reject") && passed) {
		if (storage_delete_file(storage, 'c', file_name) != 0 ||
		    storage->npending != queued || live_allocations != queued + 1)
			passed = 0;
	}
	flush_calls = select_calls;
	flush_result = storage_delete_flush(storage);
	flush_calls = select_calls - flush_calls;
	flush_empty = empty_selects;
	if (flush_result != 0 || storage->npending || queued || empty_selects ||
	    live_allocations != 1 || invalid_frees) passed = 0;
	end_result = storage_delete_end(storage);
	if (end_result != 0 || close_calls != 1 || live_allocations || invalid_frees)
		passed = 0;
	leftovers = live_allocations;
	printf("{\"scenario\":\"%s\",\"initial\":%d,\"parameter\":%d,\"passed\":%s,\"actual\":%d,\"expected\":%d,\"pending_after_attempt\":%zu,\"queued_after_attempt\":%zu,\"allocations_after_attempt\":%zu,\"flush_result\":%d,\"flush_calls\":%d,\"empty_flush_iterations\":%d,\"end_result\":%d,\"close_calls\":%d,\"leftover_allocations\":%zu,\"invalid_frees\":%zu}\n",
	    scenario, initial, parameter, passed ? "true" : "false", actual, expected,
	    after_pending, after_queued, after_allocations, flush_result, flush_calls,
	    flush_empty, end_result, close_calls, leftovers, invalid_frees);
	/* Record failure before cleaning synthetic leftovers; no leak is hidden. */
	for (i = 0; i < 4096; i++) if (allocations[i]) tracked_free(allocations[i]);
	return (passed ? 0 : 1);
}
