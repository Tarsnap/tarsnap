#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "keyfile.h"
#include "humansize.h"
#include "readpass.h"
#include "warnp.h"

static void usage(void);

static void
usage(void)
{

	fprintf(stderr, "usage: tarsnap-keymgmt %s %s %s %s key-file ...\n",
	    "--outkeyfile new-key-file", "[--passphrased]",
	    "[--passphrase-mem maxmem]", "[-r] [-w] [-d] [--nuke]");
	exit(1);

	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	const char * newkeyfile = NULL;
	int keyswanted = 0;
	char * tok, * brkb = NULL, * eptr;
	long keynum;
	uint64_t machinenum = (uint64_t)(-1);
	uint64_t kfmachinenum;
	const char * missingkey;
	int passphrased = 0;
	uint64_t maxmem = 0;
	char * passphrase;

	WARNP_INIT;

	/* Initialize key cache. */
	if (crypto_keys_init()) {
		warnp("Key cache initialization failed");
		exit(1);
	}

	/* Look for command-line options. */
	while (--argc > 0) {
		argv++;

		if (strcmp(argv[0], "--outkeyfile") == 0) {
			if ((newkeyfile != NULL) || (argc < 2))
				usage();
			newkeyfile = argv[1];
			argv++; argc--;
		} else if (strcmp(argv[0], "-r") == 0) {
			keyswanted |= CRYPTO_KEYMASK_READ;
		} else if (strcmp(argv[0], "-w") == 0) {
			keyswanted |= CRYPTO_KEYMASK_WRITE;
		} else if (strcmp(argv[0], "-d") == 0) {
			/*
			 * Deleting data requires both delete authorization
			 * and being able to read archives -- we need to be
			 * able to figure out which bits are part of the
			 * archive.
			 */
			keyswanted |= CRYPTO_KEYMASK_READ;
			keyswanted |= CRYPTO_KEYMASK_AUTH_DELETE;
		} else if (strcmp(argv[0], "--nuke") == 0) {
			keyswanted |= CRYPTO_KEYMASK_AUTH_DELETE;
		} else if (strcmp(argv[0], "--keylist") == 0) {
			/*
			 * This is a deliberately undocumented option used
			 * mostly for testing purposes; it allows a list of
			 * keys to be specified according to their numbers in
			 * crypto/crypto.h instead of using the predefined
			 * sets of "read", "write" and "delete" keys.
			 */
			if (argc < 2)
				usage();
			for (tok = strtok_r(argv[1], ",", &brkb);
			     tok;
			     tok = strtok_r(NULL, ",", &brkb)) {
				keynum = strtol(tok, &eptr, 0);
				if ((eptr == tok) ||
				    (keynum < 0) || (keynum > 32)) {
					warn0("Not a valid key number: %s",
					    tok);
					exit(1);
				}
				keyswanted |= 1 << keynum;
			}
			argv++; argc--;
		} else if (strcmp(argv[0], "--passphrase-mem") == 0) {
			if ((maxmem != 0) || (argc < 2))
				usage();
			if (humansize_parse(argv[1], &maxmem)) {
				warnp("Cannot parse --passphrase-mem"
				    " argument: %s", argv[1]);
				exit(1);
			}
			argv++; argc--;
		} else if (strcmp(argv[0], "--passphrased") == 0) {
			passphrased = 1;
		} else {
			/* Key files follow. */
			break;
		}
	}

	/* We should have an output key file. */
	if (newkeyfile == NULL)
		usage();

	/*
	 * It doesn't make sense to specify --passphrase-mem if we're not
	 * using a passphrase.
	 */
	if ((maxmem != 0) && (passphrased == 0))
		usage();

	/* Read the specified key files. */
	while (argc-- > 0) {
		/*
		 * Suck in the key file.  We could mask this to only load the
		 * keys we want to copy, but there's no point really since we
		 * export keys selectively.
		 */
		if (keyfile_read(argv[0], &kfmachinenum, ~0)) {
			warnp("Cannot read key file: %s", argv[0]);
			exit(1);
		}

		/*
		 * Check that we're not using key files which belong to
		 * different machines.
		 */
		if (machinenum == (uint64_t)(-1)) {
			machinenum = kfmachinenum;
		} else if (machinenum != kfmachinenum) {
			warn0("Keys from %s do not belong to the "
			    "same machine as earlier keys", argv[0]);
			exit(1);
		}

		/* Move on to the next file. */
		argv++;
	}

	/* Make sure that we have the necessary keys. */
	if ((missingkey = crypto_keys_missing(keyswanted)) != NULL) {
		warn0("The %s key is required but not in any input key files",
		    missingkey);
		exit(1);
	}

	/* If the user wants to passphrase the keyfile, get the passphrase. */
	if (passphrased != 0) {
		if (readpass(&passphrase,
		    "Please enter passphrase for keyfile encryption",
		    "Please confirm passphrase for keyfile encryption", 1)) {
			warnp("Error reading password");
			exit(1);
		}
	} else {
		passphrase = NULL;
	}

	/* Write out new key file. */
	if (keyfile_write(newkeyfile, machinenum, keyswanted,
	    passphrase, maxmem, 1.0))
		exit(1);

	/* Success! */
	return (0);
}
