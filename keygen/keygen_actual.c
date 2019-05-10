#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "crypto.h"
#include "insecure_memzero.h"
#include "keyfile.h"
#include "keygen.h"
#include "readpass.h"
#include "sysendian.h"
#include "tarsnap_opt.h"
#include "warnp.h"

int
keygen_actual(struct register_internal * C, const char * keyfilename,
	const int passphrased, const uint64_t maxmem,
	const double maxtime,
	const char *oldkeyfilename)
{
	FILE * keyfile;
	char * passphrase = NULL;
	int keymask = CRYPTO_KEYMASK_USER;
	uint64_t dummy;

	/* Sanity-check the user name. */
	if (strlen(C->user) > 255) {
		fprintf(stderr, "User name too long: %s\n", C->user);
		goto err0;
	}
	if (strlen(C->user) == 0) {
		fprintf(stderr, "User name must be non-empty\n");
		goto err0;
	}

	/* Sanity-check the machine name. */
	if (strlen(C->name) > 255) {
		fprintf(stderr, "Machine name too long: %s\n", C->name);
		goto err0;
	}
	if (strlen(C->name) == 0) {
		fprintf(stderr, "Machine name must be non-empty\n");
		goto err0;
	}

	/* Sanity-check the memory size. */
	if (maxmem > SIZE_MAX) {
		fprintf(stderr, "Passphrase memory size is too large\n");
		goto err0;
	}

	/* Get a password. */
	if (readpass(&C->passwd, "Enter tarsnap account password", NULL, 0)) {
		warnp("Error reading password");
		goto err0;
	}

	/*
	 * Create key file -- we do this now rather than later so that we
	 * avoid registering with the server if we won't be able to create
	 * the key file later.
	 */
	if ((keyfile = keyfile_write_open(keyfilename)) == NULL) {
		warnp("Cannot create %s", keyfilename);
		goto err1;
	}

	/* Initialize key cache. */
	if (crypto_keys_init()) {
		warnp("Key cache initialization failed");
		goto err3;
	}

	/* keyregen (with oldkeyfilename) only regenerates certain keys. */
	if (oldkeyfilename != NULL) {
		/*
		 * Load the keys CRYPTO_KEY_HMAC_{CHUNK, NAME, CPARAMS}
		 * from the old key file, since these are the keys which need
		 * to be consistent in order for two key sets to be
		 * compatible.  (CHUNK and NAME are used to compute the
		 * 32-byte keys for blocks; CPARAMS is used to compute
		 * parameters used to split a stream of bytes into chunks.)
		 */
		if (keyfile_read(oldkeyfilename, &dummy,
		    CRYPTO_KEYMASK_HMAC_CHUNK |
		    CRYPTO_KEYMASK_HMAC_NAME |
		    CRYPTO_KEYMASK_HMAC_CPARAMS, 0, 1)) {
			warnp("Error reading old key file");
			goto err3;
		}

		/*
		 * Adjust the keymask to avoid regenerating keys we read from
		 * the old keyfile.
		 */
		keymask &= ~CRYPTO_KEYMASK_HMAC_CHUNK &
		    ~CRYPTO_KEYMASK_HMAC_NAME &
		    ~CRYPTO_KEYMASK_HMAC_CPARAMS;
	}

	/* Generate keys. */
	if (crypto_keys_generate(keymask)) {
		warnp("Error generating keys");
		goto err3;
	}

	/* Register the keys with the server. */
	if (keygen_network_register(C) != 0)
		goto err3;

	/* Exit with a code of 1 if we couldn't register. */
	if (C->machinenum == (uint64_t)(-1))
		goto err3;

	/* If the user wants to passphrase the keyfile, get the passphrase. */
	if (passphrased != 0) {
		if (readpass(&passphrase,
		    "Please enter passphrase for keyfile encryption",
		    "Please confirm passphrase for keyfile encryption", 1)) {
			warnp("Error reading password");
			goto err3;
		}
	}

	/* Write keys to file. */
	if (keyfile_write_file(keyfile, C->machinenum,
	    CRYPTO_KEYMASK_USER, passphrase, (size_t)maxmem, maxtime))
		goto err3;

	/* Close the key file. */
	if (fclose(keyfile)) {
		warnp("Error closing key file");
		goto err2;
	}

	/* Free allocated memory.  C->passwd is a NUL-terminated string. */
	insecure_memzero(C->passwd, strlen(C->passwd));
	free(C->passwd);

	/* Free passphrase, if used.  passphrase is a NUL-terminated string. */
	if (passphrase != NULL) {
		insecure_memzero(passphrase, strlen(passphrase));
		free(passphrase);
	}

	/* Success! */
	return (0);

err3:
	fclose(keyfile);
err2:
	unlink(keyfilename);
err1:
	insecure_memzero(C->passwd, strlen(C->passwd));
	free(C->passwd);
	if (passphrase != NULL) {
		insecure_memzero(passphrase, strlen(passphrase));
		free(passphrase);
	}
err0:
	/* Failure! */
	return (-1);
}


