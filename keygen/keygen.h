#ifndef _KEYGEN_H_
#define _KEYGEN_H_

#include <stdint.h>

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

int keygen_network_register(struct register_internal * C);

/**
 * Create key files (either new keys or regenerated keys) and
 * register with the server.  ${C} is general information for
 * keygen code.  ${keyfilename} is the new key filename.
 * ${passphrased} and ${maxmem} are command-line arguments for
 * adding a passphrase to the key and how much memory scrypt can
 * use, respectively.  ${oldkeyfilename} is the old key filename
 * for keyregen, and must be NULL for keygen (a new key).
 */
int keygen_actual(struct register_internal * C,
		const char * keyfilename, const int passphrased,
		const uint64_t maxmem, const double maxtime,
		const char * oldkeyfilename);

#endif /* !_KEYGEN_H_ */
