/* Exercise storage_directory_read() output ownership at the close boundary. */
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
	if ((p != NULL) && (p == allocated))
		assert(freed++ == 0);
	free(p);
}

#define malloc observed_malloc
#define free observed_free
#include "../../tar/storage/storage_directory.c"
#undef free
#undef malloc

static int close_fails;
static int dummy;
static void * operation_cookie;

NETPACKET_CONNECTION *
netpacket_open(const char * agent)
{
	assert(agent != NULL);
	return ((NETPACKET_CONNECTION *)&dummy);
}

int
netpacket_close(NETPACKET_CONNECTION * npc)
{
	assert(npc == (NETPACKET_CONNECTION *)&dummy);
	return (close_fails ? -1 : 0);
}

int
netpacket_op(NETPACKET_CONNECTION * npc, sendpacket_callback * send,
    void * cookie)
{
	int rc;

	assert(npc == (NETPACKET_CONNECTION *)&dummy);
	operation_cookie = cookie;
	rc = send(cookie, npc);
	operation_cookie = NULL;
	return (rc);
}

int
netpacket_transaction_getnonce(NETPACKET_CONNECTION * npc, uint64_t machine,
    handlepacket_callback * callback)
{
	uint8_t nonce[32] = {0};

	assert(npc == (NETPACKET_CONNECTION *)&dummy);
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
	assert((a != NULL) && (b != NULL) && (alen == 32) && (blen == 32));
	memset(out, 0, 32);
	return (0);
}

int
netpacket_hmac_verify(uint8_t type, const uint8_t nonce[32],
    const uint8_t * buf, size_t len, int key)
{
	(void)nonce;
	(void)key;
	assert(type == NETPACKET_DIRECTORY_RESPONSE);
	assert(buf != NULL);
	assert(len == 70);
	return (0);
}

int
netpacket_directory(NETPACKET_CONNECTION * npc, uint64_t machine,
    uint8_t class, const uint8_t start[32], const uint8_t snonce[32],
    const uint8_t cnonce[32], int key, handlepacket_callback * callback)
{
	uint8_t packet[102] = {0};

	(void)snonce;
	(void)cnonce;
	assert(npc == (NETPACKET_CONNECTION *)&dummy);
	assert((machine == 17) && (class == 'm') && (key == 0));
	packet[1] = class;
	memcpy(packet + 2, start, 32);
	be32enc(packet + 34, 1);
	packet[38] = 1;
	return (callback(operation_cookie, npc, NETWORK_STATUS_OK,
	    NETPACKET_DIRECTORY_RESPONSE, packet, sizeof(packet)));
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
	return (0);
}

static void
run_case(int fail_close)
{
	uint8_t sentinel;
	uint8_t * files = &sentinel;
	size_t nfiles = 91;
	int rc;

	allocated = NULL;
	freed = 0;
	close_fails = fail_close;
	rc = storage_directory_read(17, 'm', 0, &files, &nfiles);

	if (fail_close) {
		assert(rc == -1);
		assert(files == &sentinel);
		assert(nfiles == 91);
		assert(freed == 1);
	} else {
		assert(rc == 0);
		assert(files == allocated);
		assert(nfiles == 1);
		assert(freed == 0);
		assert(files[0] == 1);
		observed_free(files);
	}
}

int
main(void)
{
	run_case(0);
	run_case(1);
	puts("PASS: storage_directory_read output ownership");
	return (0);
}
