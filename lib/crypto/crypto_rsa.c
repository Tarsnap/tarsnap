#include "bsdtar_platform.h"

#include <string.h>
#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rsa.h>

#include "crypto_entropy.h"
#include "crypto_verify_bytes.h"
#include "sysendian.h"
#include "warnp.h"

#include "crypto_internal.h"

#include "crypto.h"

/**
 * crypto_MGF1(seed, seedlen, buf, buflen):
 * The MGF1 mask generation function, as specified in RFC 3447.
 */
void
crypto_MGF1(uint8_t * seed, size_t seedlen, uint8_t * buf, size_t buflen)
{
	uint8_t hbuf[32];
	size_t pos;
	uint32_t i;
	uint8_t C[4];

	/* Iterate through the buffer. */
	for (pos = 0; pos < buflen; pos += 32) {
		/* The ith block starts at position i * 32. */
		i = pos / 32;

		/* Convert counter to big-endian format. */
		be32enc(C, i);

		/* Compute the hash of (seed || C). */
		if (crypto_hash_data_2(CRYPTO_KEY_HMAC_SHA256, seed, seedlen,
		    C, 4, hbuf)) {
			warn0("Programmer error: "
			    "SHA256 should never fail");
			abort();
		}

		/* Copy as much data as needed. */
		if (buflen - pos > 32)
			memcpy(buf + pos, hbuf, 32);
		else
			memcpy(buf + pos, hbuf, buflen - pos);
	}
}

/**
 * crypto_rsa_sign(key, data, len, sig, siglen):
 * Sign the provided data with the specified key, writing the signature
 * into ${sig}.
 */
int
crypto_rsa_sign(int key, const uint8_t * data, size_t len,
    uint8_t * sig, size_t siglen)
{
	RSA * rsa;	/* RSA key used for signing. */
	uint8_t mHash[32];
	uint8_t salt[32];
	uint8_t Mprime[72];
	uint8_t H[32];
	uint8_t DB[223];
	uint8_t dbMask[223];
	uint8_t maskedDB[223];
	uint8_t EM[256];
	size_t i;

	/* Find the required key. */
	if ((rsa = crypto_keys_lookup_RSA(key)) == NULL)
		goto err0;

	/* Make sure the key and signature buffer are the correct size. */
	if ((RSA_size(rsa) != 256) || (BN_num_bits(rsa->n) != 2048)) {
		warn0("RSA key is incorrect size");
		goto err0;
	}
	if (siglen != 256) {
		warn0("Programmer error: "
		    "signature buffer is incorrect length");
		goto err0;
	}

	/* Generate mHash as specified in EMSA-PSS-ENCODE from RFC 3447. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, data, len, mHash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Generate random salt. */
	if (crypto_entropy_read(salt, 32)) {
		warnp("Could not obtain sufficient entropy");
		goto err0;
	}

	/* Construct M'. */
	memset(Mprime, 0, 8);
	memcpy(Mprime + 8, mHash, 32);
	memcpy(Mprime + 40, salt, 32);

	/* Construct H. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, Mprime, 72, H)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Construct DB. */
	memset(DB, 0, 190);
	memset(DB + 190, 1, 1);
	memcpy(DB + 191, salt, 32);

	/* Construct dbMask and maskedDB. */
	crypto_MGF1(H, 32, dbMask, 223);
	for (i = 0; i < 223; i++)
		maskedDB[i] = DB[i] ^ dbMask[i];
	maskedDB[0] &= 0x7f;

	/* Construct EM. */
	memcpy(EM, maskedDB, 223);
	memcpy(EM + 223, H, 32);
	memset(EM + 255, 0xbc, 1);

	/* Convert EM to a signature, via RSA. */
	if (RSA_private_encrypt(256, EM, sig, rsa, RSA_NO_PADDING) != 256) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_rsa_verify(key, data, len, sig, siglen):
 * Verify that the provided signature matches the provided data.  Return 0
 * if the signature is valid, 1 if the signature is invalid, or -1 on error.
 */
int
crypto_rsa_verify(int key, const uint8_t * data, size_t len,
    const uint8_t * sig, size_t siglen)
{
	RSA * rsa;
	uint8_t EM[256];
	uint8_t mHash[32];
	uint8_t maskedDB[223];
	uint8_t H[32];
	uint8_t dbMask[223];
	uint8_t DB[223];
	uint8_t salt[32];
	uint8_t Mprime[72];
	uint8_t Hprime[32];
	size_t i;
	int rsaerr;

	/* Find the required key. */
	if ((rsa = crypto_keys_lookup_RSA(key)) == NULL)
		goto err0;

	/* Make sure the key and signature buffer are the correct size. */
	if ((RSA_size(rsa) != 256) || (BN_num_bits(rsa->n) != 2048)) {
		warn0("RSA key is incorrect size");
		goto err0;
	}
	if (siglen != 256) {
		warn0("Programmer error: "
		    "signature buffer is incorrect length");
		goto err0;
	}

	/* Convert the signature to EM, via RSA. */
	if (RSA_public_decrypt(siglen, sig, EM, rsa, RSA_NO_PADDING) != 256) {
		/*
		 * We can only distinguish between a bad signature and an
		 * internal error in OpenSSL by looking at the error code.
		 */
		rsaerr = ERR_get_error();
		if (rsaerr == RSA_R_DATA_TOO_LARGE_FOR_MODULUS)
			goto bad;

		/* Anything else is an internal error in OpenSSL. */
		warn0("%s", ERR_error_string(rsaerr, NULL));
		goto err0;
	}

	/* Generate mHash as specified in EMSA-PSS-VERIFY from RFC 3447. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, data, len, mHash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Verify rightmost octet of EM. */
	if (EM[255] != 0xbc)
		goto bad;

	/* Construct maskedDB and H. */
	memcpy(maskedDB, EM, 223);
	memcpy(H, EM + 223, 32);

	/* Verify high bit of leftmost octet of maskedDB. */
	if (maskedDB[0] & 0x80)
		goto bad;

	/* Construct dbMask and DB. */
	crypto_MGF1(H, 32, dbMask, 223);
	for (i = 0; i < 223; i++)
		DB[i] = maskedDB[i] ^ dbMask[i];

	/* Set high bit of leftmost octet of DB to zero. */
	DB[0] &= 0x7f;

	/* Verify padding in DB. */
	for (i = 0; i < 190; i++)
		if (DB[i] != 0)
			goto bad;
	if (DB[190] != 1)
		goto bad;

	/* Construct salt. */
	memcpy(salt, DB + 191, 32);

	/* Construct M'. */
	memset(Mprime, 0, 8);
	memcpy(Mprime + 8, mHash, 32);
	memcpy(Mprime + 40, salt, 32);

	/* Construct H'. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, Mprime, 72, Hprime)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Verify that H' == H. */
	if (crypto_verify_bytes(H, Hprime, 32))
		goto bad;

	/* Success! */
	return (0);

bad:
	/* Bad signature. */
	return (1);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_rsa_encrypt(key, data, len, out, outlen):
 * Encrypt the provided data with the specified key, writing the ciphertext
 * into ${out} (of length ${outlen}).
 */
int
crypto_rsa_encrypt(int key, const uint8_t * data, size_t len,
    uint8_t * out, size_t outlen)
{
	RSA * rsa;
	uint8_t lHash[32];
	uint8_t DB[223];
	uint8_t seed[32];
	uint8_t dbMask[223];
	uint8_t maskedDB[223];
	uint8_t seedMask[32];
	uint8_t maskedSeed[32];
	uint8_t EM[256];
	size_t i;

	/* Find the required key. */
	if ((rsa = crypto_keys_lookup_RSA(key)) == NULL)
		goto err0;

	/* Make sure the key and ciphertext buffer are the correct size. */
	if ((RSA_size(rsa) != 256) || (BN_num_bits(rsa->n) != 2048)) {
		warn0("RSA key is incorrect size");
		goto err0;
	}
	if (outlen != 256) {
		warn0("Programmer error: "
		    "ciphertext buffer is incorrect length");
		goto err0;
	}

	/* Make sure the input is not too long. */
	if (len > 190) {
		warn0("Programmer error: "
		   "input to crypto_rsa_encrypt is too long");
		goto err0;
	}

	/* Construct lHash as specified in RSAES-OAEP-ENCRYPT in RFC 3447. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, NULL, 0, lHash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Construct DB. */
	memcpy(DB, lHash, 32);
	memset(DB + 32, 0, 190 - len);
	memset(DB + 222 - len, 1, 1);
	memcpy(DB + 223 - len, data, len);

	/* Generate random seed. */
	if (crypto_entropy_read(seed, 32)) {
		warnp("Could not obtain sufficient entropy");
		goto err0;
	}

	/* Construct dbMask and maskedDB. */
	crypto_MGF1(seed, 32, dbMask, 223);
	for (i = 0; i < 223; i++)
		maskedDB[i] = DB[i] ^ dbMask[i];

	/* Construct seedMask and maskedSeed. */
	crypto_MGF1(maskedDB, 223, seedMask, 32);
	for (i = 0; i < 32; i++)
		maskedSeed[i] = seed[i] ^ seedMask[i];

	/* Construct EM. */
	memset(EM, 0, 1);
	memcpy(EM + 1, maskedSeed, 32);
	memcpy(EM + 33, maskedDB, 223);

	/* Convert EM to ciphertext, via RSA. */
	if (RSA_public_encrypt(256, EM, out, rsa, RSA_NO_PADDING) != 256) {
		warn0("%s", ERR_error_string(ERR_get_error(), NULL));
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_rsa_decrypt(key, data, len, out, outlen):
 * Decrypt the provided data with the specified key, writing the ciphertext
 * into ${out} (of length ${outlen}).  Set ${*outlen} to the length of the
 * plaintext, and return 0 on success, 1 if the ciphertext is invalid, or
 * -1 on error.
 */
int
crypto_rsa_decrypt(int key, const uint8_t * data, size_t len,
    uint8_t * out, size_t * outlen)
{
	RSA * rsa;
	uint8_t EM[256];
	uint8_t lHash[32];
	uint8_t baddata, paddingmask;
	uint8_t maskedSeed[32];
	uint8_t maskedDB[223];
	uint8_t seedMask[32];
	uint8_t seed[32];
	uint8_t dbMask[223];
	uint8_t DB[223];
	size_t msglen;
	size_t i;
	int rsaerr;

	/* Find the required key. */
	if ((rsa = crypto_keys_lookup_RSA(key)) == NULL)
		goto err0;

	/* Make sure the key and ciphertext buffer are the correct size. */
	if ((RSA_size(rsa) != 256) || (BN_num_bits(rsa->n) != 2048)) {
		warn0("RSA key is incorrect size");
		goto err0;
	}
	if (len != 256) {
		warn0("Programmer error: "
		    "ciphertext buffer is incorrect length");
		goto err0;
	}

	/* Make sure the plaintext buffer is large enough. */
	if (*outlen < 256) {
		warn0("Programmer error: "
		    "plaintext buffer is too small");
		goto err0;
	}

	/* Convert the ciphertext to EM, via RSA. */
	if (RSA_private_decrypt(len, data, EM, rsa, RSA_NO_PADDING) != 256) {
		/*
		 * We can only distinguish between bad ciphertext and an
		 * internal error in OpenSSL by looking at the error code.
		 */
		rsaerr = ERR_get_error();
		if (rsaerr == RSA_R_DATA_TOO_LARGE_FOR_MODULUS)
			goto bad;

		/* Anything else is an internal error in OpenSSL. */
		warn0("%s", ERR_error_string(rsaerr, NULL));
		goto err0;
	}

	/* Construct lHash as specified in RSAES-OAEP-DECRYPT in RFC 3447. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, NULL, 0, lHash)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/*
	 * The high byte of EM must be zero.  We test this later to avoid
	 * timing side channel attacks.
	 */
	baddata = EM[0];

	/* Construct maskedSeed and maskedDB. */
	memcpy(maskedSeed, EM + 1, 32);
	memcpy(maskedDB, EM + 33, 223);

	/* Construct seedMask and seed. */
	crypto_MGF1(maskedDB, 223, seedMask, 32);
	for (i = 0; i < 32; i++)
		seed[i] = maskedSeed[i] ^ seedMask[i];

	/* Construct dbMask and DB. */
	crypto_MGF1(seed, 32, dbMask, 223);
	for (i = 0; i < 223; i++)
		DB[i] = maskedDB[i] ^ dbMask[i];

	/*
	 * The leading 32 bytes of DB must be equal to lHash.  Test them all
	 * at once, simultaneous with other tests, in order to avoid timing
	 * side channel attacks.
	 */
	baddata = baddata | crypto_verify_bytes(DB, lHash, 32);

	/*
	 * Bytes 33 -- 223 of DB must be zero bytes followed by a one byte
	 * followed by the real data.  The following code will set baddata
	 * to a non-zero value if there are non-{0, 1} bytes which are not
	 * separated from the start by a 1 byte.
	 */
	paddingmask = 0xff;
	msglen = 191;
	for (i = 32; i < 223; i++) {
		/* If we're still doing padding, DB[i] should be 0 or 1. */
		baddata = baddata | (paddingmask & DB[i] & 0xfe);

		/*
		 * If baddata is still 0, paddingmask is either 0xff or 0x00
		 * depending upon whether the current byte is padding or not.
		 * Treating it as a signed integer and adding it to msglen
		 * will result in msglen holding the length of the message
		 * after the padding is removed.
		 */
		msglen += (int8_t)(paddingmask);

		/*-
		 * If baddata is still 0, there are 3 cases:
		 * 1. We're no longer looking at padding, and paddingmask is
		 *    0x00, so &ing it with something won't change it.
		 * 2. We're looking at a 0 byte of padding, paddingmask is
		 *    0xff, and we want it to remain 0xff.
		 * 3. We're looking at a 1 byte of padding, paddingmask is
		 *    0xff, and we want it to become 0x00.
		 * In all three cases, &ing the byte minus one does what we
		 * want.
		 */
		paddingmask = paddingmask & (DB[i] - 1);
	}

	/* Once we hit the end, the padding should be over. */
	baddata = baddata | paddingmask;

	/* Is the data bad? */
	if (baddata)
		goto bad;

	/* Sanity check the message length. */
	if (msglen > *outlen) {
		warn0("Programmer error: "
		    "decrypted message length is insane");
		goto err0;
	}

	/* Copy the message into the output buffer. */
	memcpy(out, DB + 223 - msglen, msglen);
	*outlen = msglen;

	/* Success! */
	return (0);

bad:
	/* Bad signature. */
	return (1);

err0:
	/* Failure! */
	return (-1);
}
