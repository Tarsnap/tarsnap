#include "bsdtar_platform.h"

#include <stdint.h>
#include <string.h>

#include "crypto.h"
#include "entropy.h"

/**
 * This system implements the HMAC_DRBG pseudo-random number generator as
 * specified in section 10.1.2 of the NIST SP 800-90 standard.  In this
 * implementation, the optional personalization_string and additional_input
 * specified in the standard are not implemented.
 */

/* Internal HMAC_DRBG state. */
static struct {
	uint8_t Key[32];
	uint8_t V[32];
	uint32_t reseed_counter;
} drbg;

/* Could be as high as 2^48 if we wanted... */
#define RESEED_INTERVAL	256

/* Limited to 2^16 by specification. */
#define GENERATE_MAXLEN	65536

static int instantiate(void);
static void update(uint8_t *, size_t);
static int reseed(void);
static void generate(uint8_t * buf, size_t buflen);

/**
 * instantiate(void):
 * Initialize the DRBG state.  (Section 10.1.2.3)
 */
static int
instantiate(void)
{
	uint8_t seed_material[48];

	/* Obtain random seed_material = (entropy_input || nonce). */
	if (entropy_read(seed_material, 48))
		return (-1);

	/* Initialize Key, V, and reseed_counter. */
	memset(drbg.Key, 0x00, 32);
	memset(drbg.V, 0x01, 32);
	drbg.reseed_counter = 1;

	/* Mix the random seed into the state. */
	update(seed_material, 48);

	/* Clean the stack. */
	memset(seed_material, 0, 48);

	/* Success! */
	return (0);
}

/**
 * update(data, datalen):
 * Update the DRBG state using the provided data.  (Section 10.1.2.2)
 */
static void
update(uint8_t * data, size_t datalen)
{
	uint8_t K[32];
	uint8_t Vx[33];

	/* Load (Key, V) into (K, Vx). */
	memcpy(K, drbg.Key, 32);
	memcpy(Vx, drbg.V, 32);

	/* K <- HMAC(K, V || 0x00 || data). */
	Vx[32] = 0x00;
	crypto_hash_data_key_2(K, 32, Vx, 33, data, datalen, K);

	/* V <- HMAC(K, V). */
	crypto_hash_data_key(K, 32, Vx, 32, Vx);

	/* If the provided data is non-Null, perform another mixing stage. */
	if (datalen != 0) {
		/* K <- HMAC(K, V || 0x01 || data). */
		Vx[32] = 0x01;
		crypto_hash_data_key_2(K, 32, Vx, 33, data, datalen, K);

		/* V <- HMAC(K, V). */
		crypto_hash_data_key(K, 32, Vx, 32, Vx);
	}

	/* Copy (K, Vx) back to (Key, V). */
	memcpy(drbg.Key, K, 32);
	memcpy(drbg.V, Vx, 32);

	/* Clean the stack. */
	memset(K, 0, 32);
	memset(Vx, 0, 33);
}

/**
 * reseed(void):
 * Reseed the DRBG state (mix in new entropy).  (Section 10.1.2.4)
 */
static int
reseed(void)
{
	uint8_t seed_material[32];

	/* Obtain random seed_material = entropy_input. */
	if (entropy_read(seed_material, 32))
		return (-1);

	/* Mix the random seed into the state. */
	update(seed_material, 32);

	/* Reset the reseed_counter. */
	drbg.reseed_counter = 1;

	/* Clean the stack. */
	memset(seed_material, 0, 32);

	/* Success! */
	return (0);
}

/**
 * generate(buf, buflen):
 * Fill the provided buffer with random bits, assuming that reseed_counter
 * is less than RESEED_INTERVAL (the caller is responsible for calling
 * reseed() as needed) and ${buflen} is less than 2^16 (the caller is
 * responsible for splitting up larger requests).  (Section 10.1.2.5)
 */
static void
generate(uint8_t * buf, size_t buflen)
{
	size_t bufpos;

	/* Iterate until we've filled the buffer. */
	for (bufpos = 0; bufpos < buflen; bufpos += 32) {
		crypto_hash_data_key(drbg.Key, 32, drbg.V, 32, drbg.V);
		if (buflen - bufpos >= 32)
			memcpy(&buf[bufpos], drbg.V, 32);
		else
			memcpy(&buf[bufpos], drbg.V, buflen - bufpos);
	}

	/* Mix up state. */
	update(NULL, 0);

	/* We're one data-generation step closer to needing a reseed. */
	drbg.reseed_counter += 1;
}

/**
 * crypto_entropy_init(void):
 * Initialize the PRNG.
 */
int
crypto_entropy_init(void)
{

	/* Initialize the DRBG. */
	return (instantiate());
}

/**
 * crypto_entropy_read(buf, buflen):
 * Fill the buffer with unpredictable bits.
 */
int
crypto_entropy_read(uint8_t * buf, size_t buflen)
{
	size_t bytes_to_provide;

	/* Loop until we've filled the buffer. */
	while (buflen > 0) {
		/* Do we need to reseed? */
		if (drbg.reseed_counter > RESEED_INTERVAL) {
			if (reseed())
				return (-1);
		}

		/* How much data are we generating in this step? */
		if (buflen > GENERATE_MAXLEN)
			bytes_to_provide = GENERATE_MAXLEN;
		else
			bytes_to_provide = buflen;

		/* Generate bytes. */
		generate(buf, bytes_to_provide);

		/* We've done part of the buffer. */
		buf += bytes_to_provide;
		buflen -= bytes_to_provide;
	}

	/* Success! */
	return (0);
}
