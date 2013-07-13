#include "bsdtar_platform.h"

#include <stdlib.h>
#include <string.h>

#include <openssl/aes.h>

#include "crypto_aesctr.h"
#include "crypto_entropy.h"
#include "crypto_verify_bytes.h"
#include "sysendian.h"
#include "warnp.h"

#include "crypto_internal.h"
#include "rwhashtab.h"

#include "crypto.h"

struct crypto_aes_key {
	AES_KEY key;			/* Expanded key. */
	uint64_t nonce;			/* Nonce, used only for encryption. */
	uint8_t key_encrypted[256];	/* AES key encrypted with encr_pub. */
};

static struct crypto_aes_key * encr_aes;
static RWHASHTAB * decr_aes_cache;

static int keygen(void);

/**
 * crypto_file_init_keys(void):
 * Initialize the keys cached by crypto_file.
 */
int
crypto_file_init_keys(void)
{

	/* We don't have an encryption key. */
	encr_aes = NULL;

	/* Create encrypted key -> AES key mapping table. */
	if ((decr_aes_cache =
	    rwhashtab_init(offsetof(struct crypto_aes_key, key_encrypted),
	    256)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Generate encr_aes. */
static int
keygen(void)
{
	uint8_t aeskey[32];

	/* Allocate memory. */
	if ((encr_aes = malloc(sizeof(struct crypto_aes_key))) == NULL)
		goto err0;

	/* Generate random key. */
	if (crypto_entropy_read(aeskey, 32))
		goto err1;

	/* Expand the key. */
	if (AES_set_encrypt_key(aeskey, 256, &encr_aes->key)) {
		warn0("error in AES_set_encrypt_key");
		goto err1;
	}

	/* We start with a nonce of zero. */
	encr_aes->nonce = 0;

	/* RSA encrypt the key. */
	if (crypto_rsa_encrypt(CRYPTO_KEY_ENCR_PUB, aeskey, 32,
	    encr_aes->key_encrypted, 256))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(encr_aes);
	encr_aes = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_file_enc(buf, len, filebuf):
 * Encrypt the buffer ${buf} of length ${len}, placing the result (including
 * encryption header and authentication trailer) into ${filebuf}.
 */
int
crypto_file_enc(const uint8_t * buf, size_t len, uint8_t * filebuf)
{
	struct crypto_aesctr * stream;

	/* If we don't have a session AES key yet, generate one. */
	if ((encr_aes == NULL) && keygen())
		goto err0;

	/* Copy encrypted key into header. */
	memcpy(filebuf, encr_aes->key_encrypted, 256);

	/* Store nonce. */
	be64enc(filebuf + 256, encr_aes->nonce);

	/* Encrypt the data. */
	if ((stream =
	    crypto_aesctr_init(&encr_aes->key, encr_aes->nonce++)) == NULL)
		goto err0;
	crypto_aesctr_stream(stream, buf, filebuf + CRYPTO_FILE_HLEN, len);
	crypto_aesctr_free(stream);

	/* Compute HMAC. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_FILE_WRITE, filebuf,
	    CRYPTO_FILE_HLEN + len, filebuf + CRYPTO_FILE_HLEN + len))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * crypto_file_dec(filebuf, len, buf):
 * Decrypt the buffer ${filebuf}, removing the encryption header and
 * authentication trailer, and place the result into ${buf} of length ${len}.
 */
int
crypto_file_dec(const uint8_t * filebuf, size_t len, uint8_t * buf)
{
	uint8_t hash[CRYPTO_FILE_TLEN];
	struct crypto_aes_key * key;
	struct crypto_aesctr * stream;
	uint64_t nonce;

	/*
	 * The AES key is 32 bytes, but the buffer is larger in order
	 * to properly detect and handle bogus encrypted keys (i.e., if
	 * more than 32 bytes were encrypted).
	 */
	uint8_t aeskey[256];
	size_t aeskeybuflen = 256;

	/* Compute HMAC. */
	if (crypto_hash_data(CRYPTO_KEY_HMAC_FILE,
	    filebuf, CRYPTO_FILE_HLEN + len, hash))
		goto err0;

	/* If the HMAC doesn't match, the file was corrupted. */
	if (crypto_verify_bytes(hash,
	    &filebuf[CRYPTO_FILE_HLEN + len], CRYPTO_FILE_TLEN))
		goto bad0;

	/* Look up key in hash table. */
	key = rwhashtab_read(decr_aes_cache, filebuf);

	/* If it's not in the hash table, construct it the hard way. */
	if (key == NULL) {
		/* Allocate memory. */
		if ((key = malloc(sizeof(struct crypto_aes_key))) == NULL)
			goto err0;

		/* RSA decrypt key. */
		switch (crypto_rsa_decrypt(CRYPTO_KEY_ENCR_PRIV, filebuf,
		    256, aeskey, &aeskeybuflen)) {
		case -1:
			/* Something went wrong. */
			goto err1;
		case 0:
			/* Decrypted.  Is the key the correct length? */
			if (aeskeybuflen == 32)
				break;
			/* FALLTHROUGH */
		case 1:
			/* The ciphertext is corrupt. */
			goto bad1;
		}

		/* Expand the AES key. */
		if (AES_set_encrypt_key(aeskey, 256, &key->key)) {
			warn0("error in AES_set_encrypt_key");
			goto err1;
		}

		/* Copy the encrypted AES key. */
		memcpy(key->key_encrypted, filebuf, 256);

		/* Insert the key into the hash table. */
		if (rwhashtab_insert(decr_aes_cache, key)) {
			warnp("error inserting key into aes cache");
			goto err1;
		}
	}

	/* Read the nonce. */
	nonce = be64dec(&filebuf[256]);

	/* Decrypt the data. */
	if ((stream = crypto_aesctr_init(&key->key, nonce)) == NULL)
		goto err0;
	crypto_aesctr_stream(stream, &filebuf[CRYPTO_FILE_HLEN], buf, len);
	crypto_aesctr_free(stream);

	/* Success! */
	return (0);

bad1:
	free(key);
bad0:
	/* File is not authentic. */
	return (1);

err1:
	free(key);
err0:
	/* Failure! */
	return (-1);
}
