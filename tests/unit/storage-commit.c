/* Actual commit/response code; a synthetic packet provider owns no account. */
#include "platform.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int observed_sleep(unsigned int);
#define sleep observed_sleep
#include "../../tar/storage/storage_transaction.c"
#undef sleep

static int mode, closes, requests, spins, sleeps, errors;
static int dummy;
static void * operation_cookie;

static unsigned int
observed_sleep(unsigned int seconds)
{
	assert(seconds == 1 && ++sleeps <= 2);
	return (0);
}

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
	return (mode == 4 || mode == 8 ? -1 : 0);
}

int
netpacket_op(NETPACKET_CONNECTION * npc, sendpacket_callback * send,
    void * cookie)
{
	int rc;

	assert(npc == (NETPACKET_CONNECTION *)&dummy);
	assert(operation_cookie == NULL);
	requests++;
	if (mode == 2)
		return (-1);
	operation_cookie = cookie;
	rc = send(cookie, npc);
	operation_cookie = NULL;
	return (rc);
}

int
network_spin(int * done)
{
	assert(*done == 1);
	spins++;
	return (mode == 3 ? -1 : 0);
}

int
netpacket_transaction_trycommit(NETPACKET_CONNECTION * npc,
    uint64_t machine, uint8_t key, const uint8_t seqnum[32],
    handlepacket_callback * callback)
{
	uint8_t packet[33] = {0};

	assert(machine == 17 && (key == 0 || key == 1));
	assert(seqnum[0] == 42 && seqnum[31] == 42);
	if ((mode == 7 || mode == 8) && requests < 3)
		packet[0] = 1;
	if (mode == 5)
		packet[0] = 2;
	return (callback(operation_cookie, npc, NETWORK_STATUS_OK,
	    NETPACKET_TRANSACTION_TRYCOMMIT_RESPONSE, packet, sizeof(packet)));
}

int
netpacket_hmac_verify(uint8_t type, const uint8_t nonce[32],
    const uint8_t * packet, size_t len, int key)
{
	assert(type == NETPACKET_TRANSACTION_TRYCOMMIT_RESPONSE);
	assert(nonce[0] == 42 && packet != NULL && len == 1);
	assert(key == CRYPTO_KEY_AUTH_PUT || key == CRYPTO_KEY_AUTH_DELETE);
	/* Authenticated/invalid fixtures only; no cryptography is claimed. */
	return (mode == 6 ? 1 : 0);
}

void
netproto_printerr_internal(int status)
{
	(void)status;
	errors++;
}

void
libcperciva_warnx(const char * fmt, ...)
{
	(void)fmt;
	abort();
}

int
main(void)
{
	uint8_t seqnum[32];
	int key, initial, modified, rc, confirmed;

	memset(seqnum, 42, sizeof(seqnum));
	for (key = 0; key <= 1; key++) {
		for (initial = 0; initial <= 1; initial++) {
			for (mode = 0; mode <= 8; mode++) {
				closes = requests = spins = sleeps = errors = 0;
				modified = initial;
				rc = storage_transaction_commit(17, seqnum,
				    (uint8_t)key, &modified);
				assert(rc == (mode == 0 || mode == 7 ? 0 : -1));
				confirmed = mode == 0 || mode == 4 ||
				    mode == 7 || mode == 8;
				assert(modified == (initial || confirmed));
				assert(closes == (mode == 1 ? 0 : 1));
				assert(sleeps == (mode == 7 || mode == 8 ? 2 : 0));
				assert(errors == (mode == 5 || mode == 6 ? 1 : 0));
			}
		}
	}
	puts("PASS: 36 commit confirmation, failure and retry scenarios");
	return (0);
}
