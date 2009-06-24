#ifndef _NETPACKET_INTERNAL_H_
#define _NETPACKET_INTERNAL_H_

#include <stdint.h>

#include "netpacket.h"

struct netpacket_op {
	sendpacket_callback * writepacket;
	int (* getbuf)(void *, uint8_t, uint8_t **, size_t);
	handlepacket_callback * handlepacket;
	void * cookie;

	/* Linked list. */
	struct netpacket_op * next;
};

struct netpacket_internal {
	/* Network protocol data. */
	char * useragent;
	NETPROTO_CONNECTION * NC;
	uint8_t packettype;
	size_t packetlen;
	uint8_t * packetbuf;

	/* State: 0 = disconnected, 1 = connecting, 2 = connected. */
	int state;

	/* Number of lost connections since the last successful operation. */
	int ndrops;

	/* Has a 'connection lost' message been printed? */
	int connlostmsgprinted;

	/* Non-zero if we have ever successfully connected. */
	int serveralive;

	/* Non-zero if a netproto_readpacket call is pending. */
	int reading;

	/* Bandwidth totals from dead connections. */
	uint64_t bytesin;
	uint64_t bytesout;

	/* Queue of uncompleted operations. */
	struct netpacket_op * pending_head;
	struct netpacket_op * pending_tail;
	struct netpacket_op * pending_current;
};

/**
 * netpacket_hmac_append(type, packetbuf, len, key):
 * HMAC (type || packetbuf[0 .. len - 1]) using the specified key and write
 * the result into packetbuf[len .. len + 31].
 */
int netpacket_hmac_append(uint8_t, uint8_t *, size_t, int);

/**
 * netpacket_op_packetsent(cookie, status):
 * Callback for packet send completion via netpacket_op interface.
 */
int netpacket_op_packetsent(void *, int);

#endif /* !_NETPACKET_INTERNAL_H_ */
