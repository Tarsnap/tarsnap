#include <stdint.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>

#include "warnp.h"

#include "crypto_entropy.h"
#include "crypto_dh_group14.h"

#include "crypto_dh.h"

static int blinded_modexp(uint8_t r[CRYPTO_DH_PUBLEN], BIGNUM * a,
    const uint8_t priv[CRYPTO_DH_PRIVLEN]);

/* Big-endian representation of 2^256. */
static uint8_t two_exp_256[] = {
	0x01,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

/**
 * blinded_modexp(r, a, priv):
 * Compute ${r} = ${a}^(2^258 + ${priv}), where ${r} and ${priv} are treated
 * as big-endian integers; and avoid leaking timing data in this process.
 */
static int
blinded_modexp(uint8_t r[CRYPTO_DH_PUBLEN], BIGNUM * a,
    const uint8_t priv[CRYPTO_DH_PRIVLEN])
{
	BIGNUM * two_exp_256_bn;
	BIGNUM * priv_bn;
	uint8_t blinding[CRYPTO_DH_PRIVLEN];
	BIGNUM * blinding_bn;
	BIGNUM * priv_blinded;
	BIGNUM * m_bn;
	BN_CTX * ctx;
	BIGNUM * r1;
	BIGNUM * r2;
	size_t rlen;

	/* Construct 2^256 in BN representation. */
	if ((two_exp_256_bn = BN_bin2bn(two_exp_256, 33, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Construct 2^258 + ${priv} in BN representation. */
	if ((priv_bn = BN_bin2bn(priv, CRYPTO_DH_PRIVLEN, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err1;
	}
	if ((!BN_add(priv_bn, priv_bn, two_exp_256_bn)) ||
	    (!BN_add(priv_bn, priv_bn, two_exp_256_bn)) ||
	    (!BN_add(priv_bn, priv_bn, two_exp_256_bn)) ||
	    (!BN_add(priv_bn, priv_bn, two_exp_256_bn))) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err2;
	}

	/* Generate blinding exponent. */
	if (crypto_entropy_read(blinding, CRYPTO_DH_PRIVLEN))
		goto err2;
	if ((blinding_bn = BN_bin2bn(blinding,
	    CRYPTO_DH_PRIVLEN, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err2;
	}
	if (!BN_add(blinding_bn, blinding_bn, two_exp_256_bn)) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err3;
	}

	/* Generate blinded exponent. */
	if ((priv_blinded = BN_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err3;
	}
	if (!BN_sub(priv_blinded, priv_bn, blinding_bn)) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err4;
	}

	/* Construct group #14 modulus in BN representation. */
	if ((m_bn = BN_bin2bn(crypto_dh_group14, 256, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err4;
	}

	/* Allocate BN context. */
	if ((ctx = BN_CTX_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err5;
	}

	/* Allocate space for storing results of exponentiations. */
	if ((r1 = BN_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err6;
	}
	if ((r2 = BN_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err7;
	}

	/* Perform modular exponentiations. */
	if (!BN_mod_exp(r1, a, blinding_bn, m_bn, ctx)) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err8;
	}
	if (!BN_mod_exp(r2, a, priv_blinded, m_bn, ctx)) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err8;
	}

	/* Compute final result and export to big-endian integer format. */
	if (!BN_mod_mul(r1, r1, r2, m_bn, ctx)) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err8;
	}
	rlen = BN_num_bytes(r1);
	if (rlen > CRYPTO_DH_PUBLEN) {
		warn0("Exponent result too large!");
		goto err8;
	}
	memset(r, 0, CRYPTO_DH_PUBLEN - rlen);
	BN_bn2bin(r1, r + CRYPTO_DH_PUBLEN - rlen);

	/* Free space allocated by BN_new. */
	BN_free(r2);
	BN_free(r1);

	/* Free context allocated by BN_CTX_new. */
	BN_CTX_free(ctx);

	/* Free space allocated by BN_bin2bn. */
	BN_free(m_bn);

	/* Free space allocated by BN_new. */
	BN_free(priv_blinded);

	/* Free space allocated by BN_bin2bn. */
	BN_free(blinding_bn);
	BN_free(priv_bn);
	BN_free(two_exp_256_bn);

	/* Success! */
	return (0);

err8:
	BN_free(r2);
err7:
	BN_free(r1);
err6:
	BN_CTX_free(ctx);
err5:
	BN_free(m_bn);
err4:
	BN_free(priv_blinded);
err3:
	BN_free(blinding_bn);
err2:
	BN_free(priv_bn);
err1:
	BN_free(two_exp_256_bn);
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_dh_generate_pub(pub, priv):
 * Compute ${pub} equal to 2^(2^258 + ${priv}) in Diffie-Hellman group #14.
 */
int
crypto_dh_generate_pub(uint8_t pub[CRYPTO_DH_PUBLEN],
    const uint8_t priv[CRYPTO_DH_PRIVLEN])
{
	BIGNUM * two;

	/* Generate BN representation for 2. */
	if ((two = BN_new()) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}
	if (!BN_set_word(two, 2)) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err1;
	}

	/* Compute pub = two^(2^258 + priv). */
	if (blinded_modexp(pub, two, priv))
		goto err1;

	/* Free storage allocated by BN_new. */
	BN_free(two);

	/* Success! */
	return (0);

err1:
	BN_free(two);
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_dh_generate(pub, priv):
 * Generate a 256-bit private key ${priv}, and compute ${pub} equal to
 * 2^(2^258 + ${priv}) mod p where p is the Diffie-Hellman group #14 modulus.
 * Both values are stored as big-endian integers.
 */
int
crypto_dh_generate(uint8_t pub[CRYPTO_DH_PUBLEN],
    uint8_t priv[CRYPTO_DH_PRIVLEN])
{

	/* Generate a random private key. */
	if (crypto_entropy_read(priv, CRYPTO_DH_PRIVLEN))
		goto err0;

	/* Compute the public key. */
	if (crypto_dh_generate_pub(pub, priv))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_dh_compute(pub, priv, key):
 * In the Diffie-Hellman group #14, compute ${pub}^(2^258 + ${priv}) and
 * write the result into ${key}.  All values are big-endian.  Note that the
 * value ${pub} is the public key produced the call to crypto_dh_generate
 * made by the *other* participant in the key exchange.
 */
int
crypto_dh_compute(const uint8_t pub[CRYPTO_DH_PUBLEN],
    const uint8_t priv[CRYPTO_DH_PRIVLEN], uint8_t key[CRYPTO_DH_KEYLEN])
{
	BIGNUM * a;

	/* Convert ${pub} into BN representation. */
	if ((a = BN_bin2bn(pub, CRYPTO_DH_PUBLEN, NULL)) == NULL) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Compute key = pub^(2^258 + priv). */
	if (blinded_modexp(key, a, priv))
		goto err1;

	/* Free storage allocated by BN_bin2bn. */
	BN_free(a);

	/* Success! */
	return (0);

err1:
	BN_free(a);
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_dh_sanitycheck(pub):
 * Sanity-check the Diffie-Hellman public value ${pub} by checking that it
 * is less than the group #14 modulus.  Return 0 if sane, -1 if insane.
 */
int
crypto_dh_sanitycheck(const uint8_t pub[CRYPTO_DH_PUBLEN])
{

	if (memcmp(pub, crypto_dh_group14, 256) >= 0)
		return (-1);

	/* Value is sane. */
	return (0);
}
