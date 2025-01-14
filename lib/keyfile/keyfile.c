#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "b64encode.h"
#include "crypto.h"
#include "insecure_memzero.h"
#include "passphrase_entry.h"
#include "readpass.h"
#include "scryptenc.h"
#include "sysendian.h"
#include "warnp.h"

#include "keyfile.h"

/**
 * Key file format:
 * keyfile == (rawkeyfile | textkeyfile)
 * textkeyfile == line*
 * line == blankline | commentline | base64line
 * blankline == EOL
 * commentline == "#" char* EOL
 * base64line == [a-zA-Z0-9+/=]+ EOL
 * EOL = "\n" | "\r" | "\r\n"
 *
 * After base-64 decoding, a base64line becomes a rawline.
 * rawline == rawlinedata rawlinechecksum
 * rawlinedata == byte+
 * rawlinechecksum == byte{6}
 * where rawlinechecksum is the first 6 bytes of SHA256(rawlinedata).
 *
 * After ignoring any blanklines and commentlines, converting base64lines to
 * rawlinedatas, and concatenating them together, a textkeyfile becomes a
 * tarsnapkeyfile.
 *
 * tarsnapkeyfile == scryptkeyfile | cookedkeyfile
 * scryptkeyfile == scrypt(cookedkeyfile)
 * cookedkeyfile == "tarsnap\0" rawkeyfile
 * rawkeyfile == machinenum keys
 * machinenum == big-endian-uint64_t
 * and keys are in the format used by crypto_keys_(im|ex)port.
 *
 * Put simply, there are three key formats:
 * 1. A raw key file (for historical reasons only).
 * 2. A base64-encoded key file.
 * 3. A base64-encoded encrypted key file.
 */

static int read_raw(const uint8_t *, size_t,
    uint64_t *, const char *, int);
static int read_plaintext(const uint8_t *, size_t,
    uint64_t *, const char *, int);
static int read_encrypted(const uint8_t *, size_t,
    uint64_t *, const char *, int, int, enum passphrase_entry, const char *);
static int read_base256(const uint8_t *, size_t,
    uint64_t *, const char *, int, int, enum passphrase_entry, const char *);
static int read_base64(const char *, size_t,
    uint64_t *, const char *, int, int, enum passphrase_entry, const char *);

static int
read_raw(const uint8_t * keybuf, size_t keylen, uint64_t * machinenum,
    const char * filename, int keys)
{

	/* Sanity-check size. */
	if (keylen < 8) {
		warn0("Key file is corrupt or truncated: %s", filename);
		goto err0;
	}

	/* Parse machine number from the first 8 bytes. */
	*machinenum = be64dec(keybuf);

	/* Parse keys from the remaining buffer. */
	if (crypto_keys_import(&keybuf[8], keylen - 8, keys))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
read_plaintext(const uint8_t * keybuf, size_t keylen, uint64_t * machinenum,
    const char * filename, int keys)
{

	/* Sanity-check size. */
	if (keylen < 8) {
		warn0("Key file is corrupt or truncated: %s", filename);
		goto err0;
	}

	/* Plaintext key files start with "tarsnap\0". */
	if (memcmp(keybuf, "tarsnap\0", 8)) {
		warn0("Key file is corrupt: %s", filename);
		goto err0;
	}

	/* The rest of the buffer is raw key data. */
	return (read_raw(&keybuf[8], keylen - 8, machinenum, filename, keys));

err0:
	/* Failure! */
	return (-1);
}

static int
read_encrypted(const uint8_t * keybuf, size_t keylen, uint64_t * machinenum,
    const char * filename, int keys, int force,
    enum passphrase_entry passphrase_entry, const char * passphrase_arg)
{
	char * pwprompt;
	char * passwd;
	uint8_t * deckeybuf;
	size_t deckeylen;
	int rc;
	struct scryptenc_params params = {0, 0.5, 86400.0, 0, 0, 0};

	/* The caller must pass a file name to be read. */
	assert(filename != NULL);

	/* Sanity-check size. */
	if (keylen == 0) {
		warn0("Key file is corrupt or truncated: %s", filename);
		goto err0;
	}

	/* Prompt the user for a password. */
	if (asprintf(&pwprompt, "Please enter passphrase for keyfile %s",
	    filename) == -1)
		goto err0;
	if (passphrase_entry_readpass(&passwd, passphrase_entry,
	    passphrase_arg, pwprompt, NULL, 1)) {
		warnp("Error reading passphrase");
		goto err1;
	}

	/*
	 * Allocate a buffer to hold the decrypted key.  At the present time
	 * (2009-06-01) this buffer only needs to be keylen-128 bytes long,
	 * since the only encrypted format we support has 128B of overhead;
	 * but in the future the scrypt code might support other formats with
	 * less overhead (but never less than zero bytes).
	 */
	if ((deckeybuf = malloc(keylen)) == NULL)
		goto err2;

	/* Decrypt the key file. */
	rc = scryptdec_buf(keybuf, keylen, deckeybuf, &deckeylen,
	    (const uint8_t *)passwd, strlen(passwd), &params, 0,
	    force);
	if (rc != SCRYPT_OK) {
		switch (rc) {
		case SCRYPT_ELIMIT:
			warnp("Error determining amount of available memory");
			break;
		case SCRYPT_ECLOCK:
			warnp("Error reading clocks");
			break;
		case SCRYPT_EKEY:
			warnp("Error computing derived key");
			break;
		case SCRYPT_EOPENSSL:
			warnp("OpenSSL error");
			break;
		case SCRYPT_ENOMEM:
			/* malloc failure */
			break;
		case SCRYPT_EINVAL:
			warn0("Input is not valid scrypt-encrypted block");
			break;
		case SCRYPT_EVERSION:
			warn0("Unrecognized scrypt format version");
			break;
		case SCRYPT_ETOOBIG:
			warn0("Decrypting file would require too much memory");
			break;
		case SCRYPT_ETOOSLOW:
			warn0("Decrypting file would take too much CPU time");
			break;
		case SCRYPT_EBIGSLOW:
			warn0("Decrypting file would take too much CPU time and memory");
			break;
		case SCRYPT_EPASS:
			warn0("Passphrase is incorrect");
			break;
		default:
			warn0("Programmer error: "
			    "Impossible error returned by scryptdec_buf");
			break;
		}
		warnp("Error decrypting key file: %s", filename);
		goto err3;
	}

	/*
	 * Don't need this any more.  To simplify error handling, we zero
	 * this here but free it later.
	 */
	insecure_memzero(passwd, strlen(passwd));

	/* Process the decrypted key file. */
	if (read_plaintext(deckeybuf, deckeylen, machinenum, filename, keys))
		goto err3;

	/* Clean up.  We only used the first deckeylen values of deckeybuf. */
	insecure_memzero(deckeybuf, deckeylen);
	free(deckeybuf);
	free(passwd);
	free(pwprompt);

	/* Success! */
	return (0);

err3:
	/*
	 * Depending on the error, we might not know how much data was written
	 * to deckeybuf, so we play safe by zeroing the entire allocated array.
	 */
	insecure_memzero(deckeybuf, keylen);
	free(deckeybuf);
err2:
	insecure_memzero(passwd, strlen(passwd));
	free(passwd);
err1:
	free(pwprompt);
err0:
	/* Failure! */
	return (-1);
}

static int
read_base256(const uint8_t * keybuf, size_t keylen, uint64_t * machinenum,
    const char * filename, int keys, int force,
    enum passphrase_entry passphrase_entry, const char * passphrase_arg)
{

	/* Sanity-check size. */
	if (keylen < 6) {
		warn0("Key file is corrupt or truncated: %s", filename);
		goto err0;
	}

	/* Is this encrypted? */
	if (memcmp(keybuf, "scrypt", 6) == 0)
		return (read_encrypted(keybuf, keylen,
		    machinenum, filename, keys, force, passphrase_entry,
		    passphrase_arg));

	/* Parse this as a plaintext keyfile. */
	return (read_plaintext(keybuf, keylen,
	    machinenum, filename, keys));

err0:
	/* Failure! */
	return (-1);
}

static int
read_base64(const char * keybuf, size_t keylen, uint64_t * machinenum,
    const char * filename, int keys, int force,
    enum passphrase_entry passphrase_entry, const char * passphrase_arg)
{
	uint8_t * decbuf;
	size_t decbuflen;
	size_t decpos;
	size_t lnum;
	size_t llen;
	size_t len;
	uint8_t hbuf[32];

	/* Sanity-check size. */
	if (keylen < 4) {
		warn0("Key file is corrupt or truncated: %s", filename);
		goto err0;
	}

	/*
	 * Allocate space for base64-decoded bytes.  The most space we can
	 * possibly require for the decoded bytes is 3/4 of the base64
	 * encoded length.
	 */
	decbuflen = (keylen / 4) * 3;
	if ((decbuf = malloc(decbuflen)) == NULL)
		goto err0;
	decpos = 0;

	/* Handle one line at once. */
	for (lnum = 1; keylen > 0; lnum++) {
		/* Look for an EOL character. */
		for (llen = 0; llen < keylen; llen++) {
			if ((keybuf[llen] == '\r') || (keybuf[llen] == '\n'))
				break;
		}

		/* If this isn't a comment or blank line, base-64 decode it. */
		if ((llen > 0) && (keybuf[0] != '#')) {
			if (b64decode(keybuf, llen, &decbuf[decpos], &len))
				goto err2;

			/* We should have at least 7 bytes... */
			if (len < 7)
				goto err2;

			/* ... because SHA256(line - last 6 bytes)... */
			if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256,
			    &decbuf[decpos], len - 6, hbuf)) {
				warn0("Programmer error: "
				    "SHA256 should never fail");
				goto err1;
			}

			/* ... should equal the last 6 bytes of the line. */
			if (memcmp(hbuf, &decbuf[decpos + len - 6], 6))
				goto err2;

			/* This line is good; advance the pointer. */
			decpos += len - 6;
		}

		/* Skip past this line. */
		keybuf += llen;
		keylen -= llen;

		/* Skip past the EOL if we're not at EOF. */
		if ((keylen > 1) &&
		    (keybuf[0] == '\r') && (keybuf[1] == '\n')) {
			keybuf += 2;
			keylen -= 2;
		} else if (keylen) {
			keybuf += 1;
			keylen -= 1;
		}
	}

	/* Process the decoded key file. */
	if (read_base256(decbuf, decpos, machinenum, filename, keys, force,
	    passphrase_entry, passphrase_arg))
		goto err1;

	/* Zero and free memory. */
	insecure_memzero(decbuf, decbuflen);
	free(decbuf);

	/* Success! */
	return (0);

err2:
	warn0("Key file is corrupt on line %zu: %s", lnum, filename);
err1:
	insecure_memzero(decbuf, decbuflen);
	free(decbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * keyfile_read(filename, machinenum, keys, force, passphrase_entry,
 *     passphrase_arg):
 * Read keys from a tarsnap key file; and return the machine # via the
 * provided pointer.  Ignore any keys not specified in the ${keys} mask.
 * If ${force} is 1, do not check whether decryption will exceed
 * the estimated available memory or time.  Use the ${passphrase_entry}
 * method to read the passphrase, using ${passphrase_arg} if applicable.
 */
int
keyfile_read(const char * filename, uint64_t * machinenum, int keys, int force,
    enum passphrase_entry passphrase_entry, const char * passphrase_arg)
{
	struct stat sb;
	uint8_t * keybuf;
	FILE * f;
	size_t keyfilelen;

	/* Open the file. */
	if ((f = fopen(filename, "r")) == NULL) {
		warnp("fopen(%s)", filename);
		goto err0;
	}

	/* Stat the file. */
	if (fstat(fileno(f), &sb)) {
		warnp("stat(%s)", filename);
		goto err1;
	}

	/* Validate keyfile size. */
	if ((sb.st_size == 0) || (sb.st_size > 1000000)) {
		warn0("Key file has unreasonable size: %s", filename);
		goto err1;
	}
	keyfilelen = (size_t)(sb.st_size);

	/* Allocate memory. */
	if ((keybuf = malloc(keyfilelen)) == NULL)
		goto err1;

	/* Read the file. */
	if (fread(keybuf, keyfilelen, 1, f) != 1) {
		warnp("fread(%s)", filename);
		goto err2;
	}
	if (fclose(f)) {
		warnp("fclose(%s)", filename);
		f = NULL;
		goto err2;
	}
	f = NULL;

	/* If this is a raw key file, process it. */
	if ((keybuf[0] == 0x00) || (keybuf[0] == 0xff)) {
		if (read_raw(keybuf, keyfilelen,
		    machinenum, filename, keys)) {
			if (errno)
				warnp("Error parsing key file: %s", filename);
			goto err2;
		}
	} else {
		/* Otherwise, try to base64 decode it. */
		if (read_base64((const char *)keybuf, keyfilelen,
		    machinenum, filename, keys, force, passphrase_entry,
		    passphrase_arg)) {
			if (errno)
				warnp("Error parsing key file: %s", filename);
			goto err2;
		}
	}

	/* Zero and free memory. */
	insecure_memzero(keybuf, keyfilelen);
	free(keybuf);

	/* Success! */
	return (0);

err2:
	insecure_memzero(keybuf, keyfilelen);
	free(keybuf);
err1:
	if (f != NULL)
		fclose(f);
err0:
	/* Failure! */
	return (-1);
}

/**
 * keyfile_write(filename, machinenum, keys, passphrase, maxmem, cputime):
 * Write a key file for the specified machine containing the specified keys.
 * If ${passphrase} is non-NULL, use up to ${cputime} seconds and ${maxmem}
 * bytes of memory to encrypt the key file.
 */
int
keyfile_write(const char * filename, uint64_t machinenum, int keys,
    char * passphrase, size_t maxmem, double cputime)
{
	FILE * f;

	/* Create key file. */
	if ((f = keyfile_write_open(filename)) == NULL) {
		warnp("Cannot create %s", filename);
		goto err0;
	}

	/* Write keys. */
	if (keyfile_write_file(f, machinenum, keys, passphrase,
	    maxmem, cputime))
		goto err2;

	/* Close the key file. */
	if (fclose(f)) {
		warnp("Error closing key file");
		goto err1;
	}

	/* Success! */
	return (0);

err2:
	fclose(f);
err1:
	unlink(filename);
err0:
	/* Failure! */
	return (-1);
}

/**
 * keyfile_write_open(filename):
 * Open a key file for writing.  Avoid race conditions.  Return a FILE *.
 */
FILE *
keyfile_write_open(const char * filename)
{
	FILE * f;
	int fd;
	int saved_errno;

	/* Attempt to create the file. */
	if ((fd = open(filename, O_WRONLY | O_CREAT | O_EXCL,
	    S_IRUSR | S_IWUSR)) == -1) {
		/* Special error message for EEXIST. */
		if (errno == EEXIST)
			warn0("Key file already exists, not overwriting: %s",
			    filename);
		goto err0;
	}

	/* Wrap the fd into a FILE. */
	if ((f = fdopen(fd, "w")) == NULL) {
		saved_errno = errno;
		goto err1;
	}

	/* Success! */
	return (f);

err1:
	unlink(filename);
	if (close(fd))
		warnp("close");
	errno = saved_errno;
err0:
	/* Failure! */
	return (NULL);
}

/**
 * keyfile_write_file(f, machinenum, keys, passphrase, maxmem, cputime):
 * Write a key file for the specified machine containing the specified keys.
 * If ${passphrase} is non-NULL, use up to ${cputime} seconds and ${maxmem}
 * bytes of memory to encrypt the key file.
 */
int
keyfile_write_file(FILE * f, uint64_t machinenum, int keys,
    char * passphrase, size_t maxmem, double cputime)
{
	uint8_t * keybuf;
	size_t keybuflen;
	uint8_t * tskeybuf;
	size_t tskeylen;
	uint8_t * encrbuf;
	int rc;
	uint8_t linebuf256[54];
	char linebuf64[73];
	size_t writepos;
	size_t linelen;
	uint8_t hbuf[32];
	double maxmemfrac = (maxmem != 0) ? 0.5 : 0.125;
	struct scryptenc_params params = {maxmem, maxmemfrac, cputime, 0, 0, 0};

	/* Export keys. */
	if (crypto_keys_export(keys, &keybuf, &keybuflen)) {
		warnp("Error exporting keys");
		goto err0;
	}

	/* Construct "cooked" key file. */
	tskeylen = keybuflen + 16;
	if ((tskeybuf = malloc(tskeylen)) == NULL)
		goto err1;
	memcpy(tskeybuf, "tarsnap\0", 8);
	be64enc(&tskeybuf[8], machinenum);
	memcpy(&tskeybuf[16], keybuf, keybuflen);

	/*
	 * Don't need this any more.  To simplify error handling, we zero
	 * this here but free it later.
	 */
	insecure_memzero(keybuf, keybuflen);

	/* If we have a passphrase, we want to encrypt. */
	if (passphrase != NULL) {
		/* Allocate space for encrypted buffer. */
		if ((encrbuf = malloc(tskeylen + 128)) == NULL)
			goto err2;

		/* Encrypt. */
		switch ((rc = scryptenc_buf(tskeybuf, tskeylen, encrbuf,
		    (uint8_t *)passphrase, strlen(passphrase),
		    &params, 0, 0))) {
		case SCRYPT_OK:
			/* Success! */
			break;
		case SCRYPT_ELIMIT:
			warnp("Error determining amount of available memory");
			break;
		case SCRYPT_ECLOCK:
			warnp("Error reading clocks");
			break;
		case SCRYPT_EKEY:
			warnp("Error computing derived key");
			break;
		case SCRYPT_ESALT:
			warnp("Error reading salt");
			break;
		case SCRYPT_EOPENSSL:
			warnp("OpenSSL error");
			break;
		case SCRYPT_ENOMEM:
			warnp("Error allocating memory");
			break;
		default:
			warn0("Programmer error: "
			    "Impossible error returned by scryptenc_buf");
			break;
		}

		/* Error out if the encryption failed. */
		if (rc != SCRYPT_OK) {
			insecure_memzero(encrbuf, tskeylen + 128);
			free(encrbuf);
			goto err2;
		}

		/* Switch key buffers. */
		insecure_memzero(tskeybuf, tskeylen);
		free(tskeybuf);
		tskeylen = tskeylen + 128;
		tskeybuf = encrbuf;
	}

	/* Base64-encode the buffer, writing it out as we go. */
	if (fprintf(f, "# START OF TARSNAP KEY FILE\n") < 0) {
		warnp("Error writing key file");
		goto err2;
	}
	for (writepos = 0; writepos < tskeylen; writepos += linelen) {
		linelen = 48;
		if (writepos + linelen > tskeylen)
			linelen = tskeylen - writepos;

		/* Copy bytes into line buffer. */
		memcpy(linebuf256, &tskeybuf[writepos], linelen);

		/* Append 6 bytes of SHA256 hash. */
		if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256,
		    linebuf256, linelen, hbuf)) {
			warn0("Programmer error: "
			    "SHA256 should never fail");
			goto err2;
		}
		memcpy(&linebuf256[linelen], hbuf, 6);

		/* Base64-encode. */
		b64encode(linebuf256, linebuf64, linelen + 6);

		/* Write out the line. */
		if (fprintf(f, "%s\n", linebuf64) < 0) {
			warnp("Error writing key file");
			goto err2;
		}
	}
	if (fprintf(f, "# END OF TARSNAP KEY FILE\n") < 0) {
		warnp("Error writing key file");
		goto err2;
	}

	/* Zero and free key buffers. */
	insecure_memzero(tskeybuf, tskeylen);
	free(tskeybuf);
	free(keybuf);

	/* Success! */
	return (0);

err2:
	insecure_memzero(tskeybuf, tskeylen);
	free(tskeybuf);
err1:
	insecure_memzero(keybuf, keybuflen);
	free(keybuf);
err0:
	/* Failure! */
	return (-1);
}
