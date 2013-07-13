#ifndef _CRYPTO_H_
#define _CRYPTO_H_

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Cryptographic keys held by user. */
#define CRYPTO_KEY_SIGN_PRIV	0
#define CRYPTO_KEY_SIGN_PUB	1
#define CRYPTO_KEY_ENCR_PRIV	2
#define CRYPTO_KEY_ENCR_PUB	3
#define CRYPTO_KEY_HMAC_FILE	4
#define CRYPTO_KEY_HMAC_CHUNK	5
#define CRYPTO_KEY_HMAC_NAME	6
#define CRYPTO_KEY_HMAC_CPARAMS	7

/* Cryptographic keys used in client-server protocol. */
/* 8 is reserved for the private part of the server root RSA key. */
#define CRYPTO_KEY_ROOT_PUB	9
#define CRYPTO_KEY_AUTH_PUT	10
#define CRYPTO_KEY_AUTH_GET	11
#define CRYPTO_KEY_AUTH_DELETE	12

/*
 * HMAC_FILE_WRITE is normally the same key as HMAC_FILE, but can be set to
 * a different value via a masked crypto_keys_import if we need to read from
 * one archive set and write to another (i.e., tarsnap-recrypt).  Because it
 * is a duplicate key, HMAC_FILE_WRITE cannot be exported or generated.
 */
/* Keys #13--18 reserved for server code. */
#define CRYPTO_KEY_HMAC_FILE_WRITE 19

/* Fake HMAC "key" to represent "just SHA256 the data". */
#define CRYPTO_KEY_HMAC_SHA256	(-1)

/* Bitmasks for use in representing multiple cryptographic keys. */
#define CRYPTO_KEYMASK_SIGN_PRIV	(1 << CRYPTO_KEY_SIGN_PRIV)
#define CRYPTO_KEYMASK_SIGN_PUB		(1 << CRYPTO_KEY_SIGN_PUB)
#define CRYPTO_KEYMASK_SIGN					\
	(CRYPTO_KEYMASK_SIGN_PRIV | CRYPTO_KEYMASK_SIGN_PUB)
#define CRYPTO_KEYMASK_ENCR_PRIV	(1 << CRYPTO_KEY_ENCR_PRIV)
#define CRYPTO_KEYMASK_ENCR_PUB		(1 << CRYPTO_KEY_ENCR_PUB)
#define CRYPTO_KEYMASK_ENCR					\
	(CRYPTO_KEYMASK_ENCR_PRIV | CRYPTO_KEYMASK_ENCR_PUB)
#define CRYPTO_KEYMASK_HMAC_FILE	(1 << CRYPTO_KEY_HMAC_FILE)
#define CRYPTO_KEYMASK_HMAC_FILE_WRITE	(1 << CRYPTO_KEY_HMAC_FILE_WRITE)
#define CRYPTO_KEYMASK_HMAC_CHUNK	(1 << CRYPTO_KEY_HMAC_CHUNK)
#define CRYPTO_KEYMASK_HMAC_NAME	(1 << CRYPTO_KEY_HMAC_NAME)
#define CRYPTO_KEYMASK_HMAC_CPARAMS	(1 << CRYPTO_KEY_HMAC_CPARAMS)
#define CRYPTO_KEYMASK_READ					\
	(CRYPTO_KEYMASK_ENCR_PRIV | CRYPTO_KEYMASK_SIGN_PUB |	\
	 CRYPTO_KEYMASK_HMAC_FILE | CRYPTO_KEYMASK_HMAC_CHUNK |	\
	 CRYPTO_KEYMASK_HMAC_NAME | CRYPTO_KEYMASK_AUTH_GET )
#define CRYPTO_KEYMASK_WRITE					\
	(CRYPTO_KEYMASK_SIGN_PRIV | CRYPTO_KEYMASK_ENCR_PUB |	\
	 CRYPTO_KEYMASK_HMAC_FILE | CRYPTO_KEYMASK_HMAC_CHUNK |	\
	 CRYPTO_KEYMASK_HMAC_NAME | CRYPTO_KEYMASK_HMAC_CPARAMS |	\
	 CRYPTO_KEYMASK_AUTH_PUT )

#define CRYPTO_KEYMASK_ROOT_PUB		(1 << CRYPTO_KEY_ROOT_PUB)
#define CRYPTO_KEYMASK_AUTH_PUT		(1 << CRYPTO_KEY_AUTH_PUT)
#define CRYPTO_KEYMASK_AUTH_GET		(1 << CRYPTO_KEY_AUTH_GET)
#define CRYPTO_KEYMASK_AUTH_DELETE	(1 << CRYPTO_KEY_AUTH_DELETE)

/* Mask for all the cryptographic keys held by users. */
#define CRYPTO_KEYMASK_USER					\
    (CRYPTO_KEYMASK_SIGN_PRIV | CRYPTO_KEYMASK_SIGN_PUB |	\
    CRYPTO_KEYMASK_ENCR_PRIV | CRYPTO_KEYMASK_ENCR_PUB |	\
    CRYPTO_KEYMASK_HMAC_FILE | CRYPTO_KEYMASK_HMAC_CHUNK |	\
    CRYPTO_KEYMASK_HMAC_NAME | CRYPTO_KEYMASK_HMAC_CPARAMS |	\
    CRYPTO_KEYMASK_AUTH_PUT | CRYPTO_KEYMASK_AUTH_GET |		\
    CRYPTO_KEYMASK_AUTH_DELETE)

/* Sizes of file encryption headers and trailers. */
#define CRYPTO_FILE_HLEN	(256 + 8)
#define CRYPTO_FILE_TLEN	32

/*
 * Sizes of Diffie-Hellman private, public, and exchanged keys.  These are
 * defined in libcperciva/crypto/crypto_dh.h, but are also included here as
 * the sizes are used in places which don't actually do Diffie-Hellman.
 */
#define CRYPTO_DH_PRIVLEN	32
#define CRYPTO_DH_PUBLEN	256
#define CRYPTO_DH_KEYLEN	256

/* Structure for holding client-server protocol cryptographic state. */
typedef struct crypto_session_internal CRYPTO_SESSION;

/**
 * crypto_keys_init(void):
 * Initialize the key cache.
 */
int crypto_keys_init(void);

/**
 * crypto_keys_import(buf, buflen, keys):
 * Import keys from the provided buffer into the key cache.  Ignore any keys
 * not specified in the mask ${keys}.
 */
int crypto_keys_import(const uint8_t *, size_t, int);

/**
 * crypto_keys_missing(keys):
 * Look for the specified keys.  If they are all present, return NULL; if
 * not, return a pointer to the name of one of the keys.
 */
const char * crypto_keys_missing(int);

/**
 * crypto_keys_export(keys, buf, buflen):
 * Export the keys specified to a buffer allocated using malloc.
 */
int crypto_keys_export(int, uint8_t **, size_t *);

/**
 * crypto_keys_generate(keys):
 * Create the keys specified.
 */
int crypto_keys_generate(int);

/**
 * crypto_keys_raw_export_auth(buf):
 * Write into the specified buffer the 32-byte write authorization key,
 * the 32-byte read authorization key, and the 32-byte delete authorization
 * key, in that order.
 */
int crypto_keys_raw_export_auth(uint8_t[96]);

/**
 * crypto_hash_data_key(key, keylen, data, len, buf):
 * Hash the provided data with the provided HMAC-SHA256 key.
 */
void crypto_hash_data_key(const uint8_t *, size_t,
    const uint8_t *, size_t, uint8_t[32]);

/**
 * crypto_hash_data_key_2(key, keylen, data0, len0, data1, len1, buf):
 * Hash the concatenation of two buffers with the provided HMAC-SHA256 key.
 */
void crypto_hash_data_key_2(const uint8_t *, size_t,
    const uint8_t *, size_t, const uint8_t *, size_t, uint8_t[32]);

/**
 * crypto_hash_data(key, data, len, buf):
 * Hash the provided data with the HMAC-SHA256 key specified; or if
 * ${key} == CRYPTO_KEY_HMAC_SHA256, just SHA256 the data.
 */
int crypto_hash_data(int, const uint8_t *, size_t, uint8_t[32]);

/**
 * crypto_hash_data_2(key, data0, len0, data1, len1, buf):
 * Hash the concatenation of two buffers, as in crypto_hash_data.
 */
int crypto_hash_data_2(int, const uint8_t *, size_t,
    const uint8_t *, size_t, uint8_t[32]);

/**
 * crypto_rsa_sign(key, data, len, sig, siglen):
 * Sign the provided data with the specified key, writing the signature
 * into ${sig}.
 */
int crypto_rsa_sign(int, const uint8_t *, size_t, uint8_t *, size_t);

/**
 * crypto_rsa_verify(key, data, len, sig, siglen):
 * Verify that the provided signature matches the provided data.  Return 0
 * if the signature is valid, 1 if the signature is invalid, or -1 on error.
 */
int crypto_rsa_verify(int, const uint8_t *, size_t, const uint8_t *, size_t);

/**
 * crypto_rsa_encrypt(key, data, len, out, outlen):
 * Encrypt the provided data with the specified key, writing the ciphertext
 * into ${out} (of length ${outlen}).
 */
int crypto_rsa_encrypt(int, const uint8_t *, size_t, uint8_t *, size_t);

/**
 * crypto_rsa_decrypt(key, data, len, out, outlen):
 * Decrypt the provided data with the specified key, writing the ciphertext
 * into ${out} (of length ${*outlen}).  Set ${*outlen} to the length of the
 * plaintext, and return 0 on success, 1 if the ciphertext is invalid, or
 * -1 on error.
 */
int crypto_rsa_decrypt(int, const uint8_t *, size_t, uint8_t *, size_t *);

/**
 * crypto_file_enc(buf, len, filebuf):
 * Encrypt the buffer ${buf} of length ${len}, placing the result (including
 * encryption header and authentication trailer) into ${filebuf}.
 */
int crypto_file_enc(const uint8_t *, size_t, uint8_t *);

/**
 * crypto_file_dec(filebuf, len, buf):
 * Decrypt the buffer ${filebuf}, removing the encryption header and
 * authentication trailer, and place the result into ${buf} of length ${len}.
 */
int crypto_file_dec(const uint8_t *, size_t, uint8_t *);

/**
 * crypto_session_init(pub, priv, nonce, mkey, encr_write, auth_write,
 *     encr_read, auth_read):
 * Compute K = ${pub}^(2^258 + ${priv}), mkey = MGF1(nonce || K, 48), and
 * return a CRYPTO_SESSION with encryption and authentication write and read
 * keys constructed from HMAC(mkey, (encr|auth)_(write|read)).
 */
CRYPTO_SESSION * crypto_session_init(uint8_t[CRYPTO_DH_PUBLEN],
    uint8_t[CRYPTO_DH_PRIVLEN], uint8_t[32], uint8_t[48],
    const char *, const char *, const char *, const char *);

/**
 * crypto_session_encrypt(CS, inbuf, outbuf, buflen):
 * Encrypt inbuf with the session write key and write ciphertext to outbuf.
 */
void crypto_session_encrypt(CRYPTO_SESSION *, const uint8_t *, uint8_t *,
    size_t);

/**
 * crypto_session_decrypt(CS, inbuf, outbuf, buflen):
 * Decrypt inbuf with the session read key and write plaintext to outbuf.
 */
void crypto_session_decrypt(CRYPTO_SESSION *, const uint8_t *, uint8_t *,
    size_t);

/**
 * crypto_session_sign(CS, buf, buflen, sig):
 * Generate sig = write_auth(buf).
 */
void crypto_session_sign(CRYPTO_SESSION *, const uint8_t *, size_t,
    uint8_t[32]);

/**
 * crypto_session_verify(CS, buf, buflen, sig):
 * Verify that sig = read_auth(buf).  Return non-zero if the signature
 * does not match.
 */
int crypto_session_verify(CRYPTO_SESSION *, const uint8_t *, size_t,
    const uint8_t[32]);

/**
 * crypto_session_free(CS):
 * Free a CRYPTO_SESSION structure.
 */
void crypto_session_free(CRYPTO_SESSION *);

/**
 * crypto_passwd_to_dh(passwd, salt, pub, priv):
 * Generate a Diffie-Hellman pair (${priv}, ${pub}), with ${pub} equal to
 * 2^(2^258 + ${priv}) modulo the group #14 modulus, and ${priv} equal to
 * HMAC(${salt}, ${passwd}), where ${passwd} is a NUL-terminated string.
 */
int crypto_passwd_to_dh(const char *, const uint8_t[32],
    uint8_t[CRYPTO_DH_PUBLEN], uint8_t[CRYPTO_DH_PRIVLEN]);

#endif /* !_CRYPTO_H_ */
