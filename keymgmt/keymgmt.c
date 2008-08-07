#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "sysendian.h"
#include "warnp.h"

static void usage(void);

static void
usage(void)
{

	fprintf(stderr, "usage: tarsnap-keymgmt %s %s %s %s key-file ...\n",
	    "--outkeyfile new-key-file", "[-r]", "[-w]", "[-d]");
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
	struct stat sb;
	uint8_t * keybuf;
	size_t keybuflen;
	FILE * f;
	uint64_t machinenum = (uint64_t)(-1);
	uint8_t machinenumvec[8];
	const char * missingkey;

	/* Initialize entropy subsystem. */
	if (crypto_entropy_init()) {
		warnp("Entropy subsystem initialization failed");
		exit(1);
	}

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
		} else {
			/* Key files follow. */
			break;
		}
	}

	/* We should have an output key file. */
	if (newkeyfile == NULL)
		usage();

	/* Read the specified key files. */
	while (argc-- > 0) {
		/* Stat the key file. */
		if (stat(argv[0], &sb)) {
			warnp("stat(%s)", argv[0]);
			exit(1);
		}

		/* Allocate memory to hold the keyfile contents. */
		if ((sb.st_size < 8) || (sb.st_size > 1000000)) {
			warn0("Key file has unreasonable size: %s", argv[0]);
			exit(1);
		}
		if ((keybuf = malloc(sb.st_size)) == NULL) {
			warn0("Out of memory");
			exit(1);
		}

		/* Read the file. */
		if ((f = fopen(argv[0], "r")) == NULL) {
			warnp("fopen(%s)", argv[0]);
			exit(1);
		}
		if (fread(keybuf, sb.st_size, 1, f) != 1) {
			warnp("fread(%s)", argv[0]);
			exit(1);
		}
		if (fclose(f)) {
			warnp("fclose(%s)", argv[0]);
			exit(1);
		}

		/*
		 * Parse the machine number from the key file or check that
		 * the machine number from the key file matches the number
		 * we already have.
		 */
		if (machinenum == (uint64_t)(-1)) {
			machinenum = be64dec(keybuf);
		} else if (machinenum != be64dec(keybuf)) {
			warn0("Keys from %s do not belong to the "
			    "same machine as earlier keys", argv[0]);
			exit(1);
		}

		/* Parse keys. */
		if (crypto_keys_import(&keybuf[8], sb.st_size - 8)) {
			warn0("Error parsing keys in %s", argv[0]);
			exit(1);
		}

		/* Free memory. */
		free(keybuf);

		/* Move on to the next file. */
		argv++;
	}

	/* Make sure that we have the necessary keys. */
	if ((missingkey = crypto_keys_missing(keyswanted)) != NULL) {
		warn0("The %s key is required but not in any input key files",
		    missingkey);
		exit(1);
	}

	/* Create key file. */
	if ((f = fopen(newkeyfile, "w")) == NULL) {
		warnp("Cannot create %s", newkeyfile);
		exit(1);
	}

	/* Set the permissions on the key file to 0600. */
	if (fchmod(fileno(f), S_IRUSR | S_IWUSR)) {
		warnp("Cannot set permissions on key file: %s", newkeyfile);
		exit(1);
	}

	/* Export keys. */
	if (crypto_keys_export(keyswanted, &keybuf, &keybuflen)) {
		warnp("Error exporting keys");
		exit(1);
	}

	/* Write keys. */
	be64enc(machinenumvec, machinenum);
	if (fwrite(machinenumvec, 8, 1, f) != 1) {
		warnp("Error writing keys");
		exit(1);
	}
	if (fwrite(keybuf, keybuflen, 1, f) != 1) {
		warnp("Error writing keys");
		exit(1);
	}

	/* Close the key file. */
	if (fclose(f)) {
		warnp("Error closing key file");
		exit(1);
	}

	/* Success! */
	return (0);
}
