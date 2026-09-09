/* Exercise the actual output-ownership boundary; transport is a fixture. */
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void * allocated;
static int freed;
static void *
observed_malloc(size_t len)
{
	void * p = malloc(len);

	assert(allocated == NULL);
	allocated = p;
	return (p);
}
static void
observed_free(void * p)
{
	if (p != NULL && p == allocated)
		assert(freed++ == 0);
	free(p);
}

#define malloc observed_malloc
#define free observed_free
#include "../../tar/storage/storage_directory.c"
#undef free
#undef malloc

static int mode, closes, operations, spins;
static size_t count;
static int dummy;

NETPACKET_CONNECTION *
netpacket_open(const char * agent)
{
	assert(agent != NULL);
	return (mode == 1 ? NULL : (NETPACKET_CONNECTION *)&dummy);
}

int
netpacket_close(NETPACKET_CONNECTION * npc)
{
	assert(npc == (NETPACKET_CONNECTION *)&dummy);
	closes++;
	return (mode == 4 ? -1 : 0);
}

static void * operation_cookie;

int
netpacket_op(NETPACKET_CONNECTION * npc, sendpacket_callback * send,
    void * cookie)
{
	int rc;

	assert(npc == (NETPACKET_CONNECTION *)&dummy);
	operations++;
	operation_cookie = cookie;
	rc = send(cookie, npc);
	operation_cookie = NULL;
	return (mode == 2 ? -1 : rc);
}

int
netpacket_transaction_getnonce(NETPACKET_CONNECTION * npc, uint64_t machine,
    handlepacket_callback * callback)
{
	uint8_t nonce[32] = {0};

	assert(machine == 17);
	return (callback(operation_cookie, npc, NETWORK_STATUS_OK,
	    NETPACKET_TRANSACTION_GETNONCE_RESPONSE, nonce, sizeof(nonce)));
}

int
crypto_entropy_read(uint8_t * buf, size_t len)
{
	memset(buf, 0, len);
	return (0);
}

int
crypto_hash_data_2(int key, const uint8_t * a, size_t alen,
    const uint8_t * b, size_t blen, uint8_t out[32])
{
	(void)key;
	assert(a != NULL && b != NULL && alen == 32 && blen == 32);
	memset(out, 0, 32);
	return (0);
}

int
netpacket_hmac_verify(uint8_t type, const uint8_t nonce[32],
    const uint8_t * buf, size_t len, int key)
{
	(void)nonce;
	(void)key;
	assert(type == NETPACKET_DIRECTORY_RESPONSE && buf != NULL);
	assert(len == 38 + 32 * count);
	/* This tests ownership, not authentication; no real keys are used. */
	return (0);
}

int
netpacket_directory(NETPACKET_CONNECTION * npc, uint64_t machine,
    uint8_t class, const uint8_t start[32], const uint8_t snonce[32],
    const uint8_t cnonce[32], int key, handlepacket_callback * callback)
{
	uint8_t packet[70 + 3 * 32] = {0};
	size_t i;

	(void)snonce;
	(void)cnonce;
	assert(machine == 17 && class == 'm' && (key == 0 || key == 1));
	packet[1] = class;
	memcpy(packet + 2, start, 32);
	be32enc(packet + 34, (uint32_t)count);
	for (i = 0; i < count; i++)
		packet[38 + i * 32] = (uint8_t)(i + 1);
	return (callback(operation_cookie, npc, NETWORK_STATUS_OK,
	    NETPACKET_DIRECTORY_RESPONSE, packet, 70 + count * 32));
}

int
netpacket_directory_readmore(NETPACKET_CONNECTION * npc,
    handlepacket_callback * callback)
{
	(void)npc;
	(void)callback;
	abort();
}

void
netproto_printerr_internal(int status)
{
	(void)status;
	abort();
}

void
libcperciva_warnx(const char * fmt, ...)
{
	(void)fmt;
	abort();
}

int
network_spin(int * done)
{
	assert(*done == 1);
	spins++;
	return (mode == 3 ? -1 : 0);
}

int
main(void)
{
	uint8_t sentinel, * files;
	size_t nfiles;
	int key, rc;

	for (key = 0; key <= 1; key++) {
		for (count = 0; count <= 3; count += 3) {
			for (mode = 0; mode <= 4; mode++) {
				allocated = NULL;
				freed = closes = operations = spins = 0;
				files = &sentinel;
				nfiles = 91;
				rc = storage_directory_read(17, 'm', key,
				    &files, &nfiles);
				assert(rc == (mode == 0 ? 0 : -1));
				assert(closes == (mode == 1 ? 0 : 1));
				assert(operations == (mode == 1 ? 0 : 1));
				assert(spins == (mode == 1 || mode == 2 ? 0 : 1));
				if (mode == 0) {
					assert(nfiles == count && files == allocated);
					assert(freed == 0);
					if (count != 0)
						assert(files[(count - 1) * 32] == count);
					observed_free(files);
				} else {
					/* Neither out-parameter is published on error. */
					assert(files == &sentinel && nfiles == 91);
				}
				assert(freed == (mode != 1 && count != 0 ? 1 : 0));
			}
		}
	}
	puts("PASS: 20 directory ownership, cleanup and output scenarios");
	return (0);
}
