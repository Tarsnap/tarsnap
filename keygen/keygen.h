#ifndef _KEYGEN_H_
#define _KEYGEN_H_

#include <stdint.h>

#include "netpacket.h" /* temporary for callback_register_send. */

struct register_internal {
	/* Parameters provided from main() to network code. */
	const char * user;
	char * passwd;
	const char * name;

	/* State information. */
	int donechallenge;
	int done;

	/* Key used to send challenge response and verify server response. */
	uint8_t register_key[32];

	/* Data returned by server. */
	uint8_t status;
	uint64_t machinenum;
};

int callback_register_send(void * cookie, NETPACKET_CONNECTION * NPC);

#endif /* !_KEYGEN_H_ */
