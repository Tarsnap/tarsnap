#include "bsdtar_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/rand.h>

#include "sysendian.h"
#include "warnp.h"

#include "crypto_internal.h"
#include "crypto.h"

static struct {
	RSA * sign_priv;
	RSA * sign_pub;
	RSA * encr_priv;
	RSA * encr_pub;
	RSA * root_pub;
	struct crypto_hmac_key * hmac_file;
	struct crypto_hmac_key * hmac_chunk;
	struct crypto_hmac_key * hmac_name;
	struct crypto_hmac_key * hmac_cparams;
	struct crypto_hmac_key * auth_put;
	struct crypto_hmac_key * auth_get;
	struct crypto_hmac_key * auth_delete;
} keycache;

/* External key data format. */
struct keyheader {
	uint8_t len[4];	/* Length of key data, little-endian. */
	uint8_t type;	/* Key type. */
	/* Key data, in key-specific format. */
};

/* Amount of entropy to use for seeding OpenSSL. */
#define RANDBUFLEN	2048

/**
 * export_key(key, buf, buflen):
 * If buf != NULL, export the specified key.  Return the key length in bytes.
 */
static uint32_t
export_key(int key, uint8_t * buf, size_t buflen)
{
	uint32_t len;

	switch (key) {
	case CRYPTO_KEY_SIGN_PRIV:
		len = crypto_keys_subr_export_RSA_priv(keycache.sign_priv,
		    buf, buflen);
		break;
	case CRYPTO_KEY_SIGN_PUB:
		len = crypto_keys_subr_export_RSA_pub(keycache.sign_pub, buf,
		    buflen);
		break;
	case CRYPTO_KEY_ENCR_PRIV:
		len = crypto_keys_subr_export_RSA_priv(keycache.encr_priv,
		    buf, buflen);
		break;
	case CRYPTO_KEY_ENCR_PUB:
		len = crypto_keys_subr_export_RSA_pub(keycache.encr_pub, buf,
		    buflen);
		break;
	case CRYPTO_KEY_HMAC_FILE:
		len = crypto_keys_subr_export_HMAC(keycache.hmac_file, buf,
		    buflen);
		break;
	case CRYPTO_KEY_HMAC_CHUNK:
		len = crypto_keys_subr_export_HMAC(keycache.hmac_chunk, buf,
		    buflen);
		break;
	case CRYPTO_KEY_HMAC_NAME:
		len = crypto_keys_subr_export_HMAC(keycache.hmac_name,
		    buf, buflen);
		break;
	case CRYPTO_KEY_HMAC_CPARAMS:
		len = crypto_keys_subr_export_HMAC(keycache.hmac_cparams,
		    buf, buflen);
		break;
	case CRYPTO_KEY_AUTH_PUT:
		len = crypto_keys_subr_export_HMAC(keycache.auth_put, buf,
		    buflen);
		break;
	case CRYPTO_KEY_AUTH_GET:
		len = crypto_keys_subr_export_HMAC(keycache.auth_get, buf,
		    buflen);
		break;
	case CRYPTO_KEY_AUTH_DELETE:
		len = crypto_keys_subr_export_HMAC(keycache.auth_delete, buf,
		    buflen);
		break;
	default:
		warn0("Unrecognized key type: %d", key);
		goto err0;
	}

	/* Did the key export fail? */
	if (len == (uint32_t)(-1))
		goto err0;

	/* Success! */
	return (len);

err0:
	/* Failure! */
	return ((uint32_t)(-1));
}

/**
 * crypto_keys_init():
 * Initialize the key cache.
 */
int
crypto_keys_init(void)
{
	uint8_t randbuf[RANDBUFLEN];

	/* No keys yet. */
	memset(&keycache, 0, sizeof(keycache));

	/* Load OpenSSL error strings. */
	ERR_load_crypto_strings();

	/* Seed OpenSSL entropy pool. */
	if (crypto_entropy_read(randbuf, RANDBUFLEN)) {
		warnp("Could not obtain sufficient entropy");
		goto err0;
	}
	RAND_seed(randbuf, RANDBUFLEN);

	/* Load server root public key. */
	if (crypto_keys_server_import_root())
		goto err0;

	/* Initialize keys owned by crypto_file. */
	if (crypto_file_init_keys())
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_import(buf, buflen):
 * Import keys from the provided buffer into the key cache.
 */
int
crypto_keys_import(uint8_t * buf, size_t buflen)
{
	struct keyheader * kh;
	uint32_t len;

	/* Loop until we've processed all the provided data. */
	while (buflen) {
		/* We must have at least a struct keyheader. */
		if (buflen < sizeof(struct keyheader)) {
			warn0("Unexpected EOF of key data");
			goto err0;
		}

		/* Parse header. */
		kh = (struct keyheader *)buf;
		buf += sizeof(struct keyheader);
		buflen -= sizeof(struct keyheader);

		/* Sanity check length. */
		len = le32dec(kh->len);
		if (len > buflen) {
			warn0("Unexpected EOF of key data");
			goto err0;
		}

		/* Parse the key. */
		switch (kh->type) {
		case CRYPTO_KEY_SIGN_PRIV:
			if (crypto_keys_subr_import_RSA_priv(
			    &keycache.sign_priv, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_SIGN_PUB:
			if (crypto_keys_subr_import_RSA_pub(
			    &keycache.sign_pub, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_ENCR_PRIV:
			if (crypto_keys_subr_import_RSA_priv(
			    &keycache.encr_priv, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_ENCR_PUB:
			if (crypto_keys_subr_import_RSA_pub(
			    &keycache.encr_pub, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_HMAC_FILE:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.hmac_file, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_HMAC_CHUNK:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.hmac_chunk, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_HMAC_NAME:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.hmac_name, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_HMAC_CPARAMS:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.hmac_cparams, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_ROOT_PUB:
			if (crypto_keys_subr_import_RSA_pub(
			    &keycache.root_pub, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_AUTH_PUT:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.auth_put, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_AUTH_GET:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.auth_get, buf, len))
				goto err0;
			break;
		case CRYPTO_KEY_AUTH_DELETE:
			if (crypto_keys_subr_import_HMAC(
			    &keycache.auth_delete, buf, len))
				goto err0;
			break;
		default:
			warn0("Unrecognized key type: %d", kh->type);
			goto err0;
		}

		/* Move on to the next key. */
		buf += len;
		buflen -= len;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_export(keys, buf, buflen):
 * Export the keys specified to a buffer allocated using malloc.
 */
int
crypto_keys_export(int keys, uint8_t ** buf, size_t * buflen)
{
	struct keyheader * kh;
	size_t bufpos;
	uint32_t len;
	int key;

	/* Compute the necessary buffer length. */
	*buflen = 0;
	for (key = 0; key < (int)(sizeof(int) * 8); key++)
	    if ((keys >> key) & 1) {
		/* Determine the length needed for this key. */
		len = export_key(key, NULL, 0);
		if (len == (uint32_t)(-1))
			goto err0;

		/* Add to buffer length, making sure to avoid overflow. */
		if (*buflen > *buflen + len) {
			errno = ENOMEM;
			goto err0;
		}
		*buflen += len;
		if (*buflen > *buflen + sizeof(struct keyheader)) {
			errno = ENOMEM;
			goto err0;
		}
		*buflen += sizeof(struct keyheader);
	}

	/* Allocate memory. */
	if ((*buf = malloc(*buflen)) == NULL)
		goto err0;

	/* Export keys. */
	bufpos = 0;
	for (key = 0; key < (int)(sizeof(int) * 8); key++)
	    if ((keys >> key) & 1) {
		/* Sanity check remaining buffer length. */
		if (*buflen - bufpos < sizeof(struct keyheader)) {
			warn0("Programmer error");
			goto err1;
		}

		/* Export key. */
		len = export_key(key,
		    *buf + (bufpos + sizeof(struct keyheader)),
		    *buflen - (bufpos + sizeof(struct keyheader)));
		if (len == (uint32_t)(-1))
			goto err1;

		/* Write key header. */
		kh = (struct keyheader *)(*buf + bufpos);
		kh->type = key;
		le32enc(kh->len, len);

		/* Advance buffer position. */
		bufpos += sizeof(struct keyheader) + len;
	}

	/* Sanity-check -- we should have filled the buffer. */
	if (bufpos != *buflen) {
		warn0("Programmer error");
		goto err1;
	}

	/* Success! */
	return (0);

err1:
	free(*buf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_generate(keys):
 * Create the keys specified.
 */
int
crypto_keys_generate(int keys)
{

	/* Archive signing RSA key. */
	if (keys & CRYPTO_KEYMASK_SIGN_PRIV) {
		if ((keys & CRYPTO_KEYMASK_SIGN_PUB) == 0) {
			warn0("Cannot generate %s without %s",
			    "private key", "public key");
			goto err0;
		}
		if (crypto_keys_subr_generate_RSA(&keycache.sign_priv,
		    &keycache.sign_pub))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_SIGN_PRIV;
		keys &= ~CRYPTO_KEYMASK_SIGN_PUB;
	}
	if (keys & CRYPTO_KEYMASK_SIGN_PUB) {
		warn0("Cannot generate %s without %s",
		    "public key", "private key");
		goto err0;
	}

	/* Encryption RSA key. */
	if (keys & CRYPTO_KEYMASK_ENCR_PRIV) {
		if ((keys & CRYPTO_KEYMASK_ENCR_PUB) == 0) {
			warn0("Cannot generate %s without %s",
			    "private key", "public key");
			goto err0;
		}
		if (crypto_keys_subr_generate_RSA(&keycache.encr_priv,
		    &keycache.encr_pub))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_ENCR_PRIV;
		keys &= ~CRYPTO_KEYMASK_ENCR_PUB;
	}
	if (keys & CRYPTO_KEYMASK_ENCR_PUB) {
		warn0("Cannot generate %s without %s",
		    "public key", "private key");
		goto err0;
	}

	/* File HMAC key. */
	if (keys & CRYPTO_KEYMASK_HMAC_FILE) {
		if (crypto_keys_subr_generate_HMAC(&keycache.hmac_file))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_HMAC_FILE;
	}

	/* Chunk HMAC key. */
	if (keys & CRYPTO_KEYMASK_HMAC_CHUNK) {
		if (crypto_keys_subr_generate_HMAC(&keycache.hmac_chunk))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_HMAC_CHUNK;
	}

	/* Name HMAC key. */
	if (keys & CRYPTO_KEYMASK_HMAC_NAME) {
		if (crypto_keys_subr_generate_HMAC(&keycache.hmac_name))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_HMAC_NAME;
	}

	/* Chunkfication parameters HMAC key. */
	if (keys & CRYPTO_KEYMASK_HMAC_CPARAMS) {
		if (crypto_keys_subr_generate_HMAC(&keycache.hmac_cparams))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_HMAC_CPARAMS;
	}

	/* Write transaction authorization key. */
	if (keys & CRYPTO_KEYMASK_AUTH_PUT) {
		if (crypto_keys_subr_generate_HMAC(&keycache.auth_put))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_AUTH_PUT;
	}

	/* Read transaction authorization key. */
	if (keys & CRYPTO_KEYMASK_AUTH_GET) {
		if (crypto_keys_subr_generate_HMAC(&keycache.auth_get))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_AUTH_GET;
	}

	/* Delete transaction authorization key. */
	if (keys & CRYPTO_KEYMASK_AUTH_DELETE) {
		if (crypto_keys_subr_generate_HMAC(&keycache.auth_delete))
			goto err0;

		keys &= ~CRYPTO_KEYMASK_AUTH_DELETE;
	}

	/* Anything left? */
	if (keys) {
		warn0("Unrecognized key types: %08x", keys);
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_raw_export_auth(buf):
 * Write into the specified buffer the 32-byte write authorization key,
 * the 32-byte read authorization key, and the 32-byte delete authorization
 * key, in that order.
 */
int
crypto_keys_raw_export_auth(uint8_t buf[96])
{
	uint32_t len;

	len = export_key(CRYPTO_KEY_AUTH_PUT, buf, 32);
	if (len == (uint32_t)(-1))
		goto err0;
	if (len != 32) {
		warn0("Programmer error: "
		    "Incorrect HMAC key size: %u", (unsigned int)len);
		goto err0;
	}

	len = export_key(CRYPTO_KEY_AUTH_GET, buf + 32, 32);
	if (len == (uint32_t)(-1))
		goto err0;
	if (len != 32) {
		warn0("Programmer error: "
		    "Incorrect HMAC key size: %u", (unsigned int)len);
		goto err0;
	}

	len = export_key(CRYPTO_KEY_AUTH_DELETE, buf + 64, 32);
	if (len == (uint32_t)(-1))
		goto err0;
	if (len != 32) {
		warn0("Programmer error: "
		    "Incorrect HMAC key size: %u", (unsigned int)len);
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_keys_lookup_RSA(key):
 * Return the requested RSA key.
 */
RSA *
crypto_keys_lookup_RSA(int key)
{
	RSA * rsa;

	/* Look up the key. */
	switch (key) {
	case CRYPTO_KEY_SIGN_PRIV:
		rsa = keycache.sign_priv;
		break;
	case CRYPTO_KEY_SIGN_PUB:
		rsa = keycache.sign_pub;
		break;
	case CRYPTO_KEY_ENCR_PRIV:
		rsa = keycache.encr_priv;
		break;
	case CRYPTO_KEY_ENCR_PUB:
		rsa = keycache.encr_pub;
		break;
	case CRYPTO_KEY_ROOT_PUB:
		rsa = keycache.root_pub;
		break;
	default:
		warn0("Programmer error: "
		    "invalid key (%d) in crypto_keys_lookup_RSA", key);
		goto err0;
	}

	/* Make sure that we have the key. */
	if (rsa == NULL) {
		warn0("Programmer error: "
		    "key %d not available in crypto_keys_lookup_RSA", key);
		goto err0;
	}

	/* Success! */
	return (rsa);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * crypto_keys_lookup_HMAC(key):
 * Return the requested HMAC key
 */
struct crypto_hmac_key *
crypto_keys_lookup_HMAC(int key)
{
	struct crypto_hmac_key * hkey;

	/* Look up the key. */
	switch (key) {
	case CRYPTO_KEY_HMAC_FILE:
		hkey = keycache.hmac_file;
		break;
	case CRYPTO_KEY_HMAC_CHUNK:
		hkey = keycache.hmac_chunk;
		break;
	case CRYPTO_KEY_HMAC_NAME:
		hkey = keycache.hmac_name;
		break;
	case CRYPTO_KEY_HMAC_CPARAMS:
		hkey = keycache.hmac_cparams;
		break;
	case CRYPTO_KEY_AUTH_PUT:
		hkey = keycache.auth_put;
		break;
	case CRYPTO_KEY_AUTH_GET:
		hkey = keycache.auth_get;
		break;
	case CRYPTO_KEY_AUTH_DELETE:
		hkey = keycache.auth_delete;
		break;
	default:
		warn0("Programmer error: "
		    "invalid key (%d) in crypto_keys_lookup_HMAC", key);
		goto err0;
	}

	/* Make sure that we have the key. */
	if (hkey == NULL) {
		warn0("Programmer error: "
		    "key %d not available in crypto_keys_lookup_HMAC", key);
		goto err0;
	}

	/* Success! */
	return (hkey);

err0:
	/* Failure! */
	return (NULL);
}
