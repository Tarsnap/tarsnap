#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "humansize.h"
#include "keyfile.h"
#include "readpass.h"
#include "warnp.h"

static void usage(void);

static void
usage(void)
{

	fprintf(stderr, "usage: tarsnap-keymgmt %s %s %s %s %s key-file ...\n",
	    "--outkeyfile new-key-file", "[--passphrased]",
	    "[--passphrase-mem maxmem]", "[--passphrase-time maxtime]",
	    "[-r] [-w] [-d] [--nuke]");
	fprintf(stderr, "       tarsnap-keymgmt --print-key-id key-file\n");
	fprintf(stderr, "       tarsnap-keymgmt --print-key-permissions "
	    "key-file\n");
	exit(1);

	/* NOTREACHED */
}

static void
print_id(const char *keyfilename)
{
	uint64_t machinenum = (uint64_t)(-1);

	/* Read keyfile and machine name. */
	if (keyfile_read(keyfilename, &machinenum, ~0)) {
		warnp("Cannot read key file: %s", keyfilename);
		exit(1);
	}

	/* Print key ID. */
	fprintf(stdout, "%" PRIu64 "\n", machinenum);
	exit(0);

	/* NOTREACHED */
}
	
static void
print_permissions(const char *keyfilename)
{
	uint64_t machinenum = (uint64_t)(-1);
	int has_read;
	int has_write;
	int has_delete;

	/* Read keyfile and machine name. */
	if (keyfile_read(keyfilename, &machinenum, ~0)) {
		warnp("Cannot read key file: %s", keyfilename);
		exit(1);
	}

	/* Determine permissions. */
	has_read = (crypto_keys_missing(CRYPTO_KEYMASK_READ) == NULL);
	has_write = (crypto_keys_missing(CRYPTO_KEYMASK_WRITE) == NULL);
	has_delete = (crypto_keys_missing(CRYPTO_KEYMASK_AUTH_DELETE) == NULL);

	/* Print key permissions. */
	fprintf(stdout, "This key has permissions for: ");
	if (has_read && has_write && has_delete)
		fprintf(stdout, "reading, writing, and deleting.\n");
	if (has_read && has_write && !has_delete)
		fprintf(stdout, "reading and writing.\n");
	if (has_read && !has_write && has_delete)
		fprintf(stdout, "reading and deleting.\n");
	if (has_read && !has_write && !has_delete)
		fprintf(stdout, "reading.\n");
	if (!has_read && has_write && has_delete)
		fprintf(stdout, "writing and nuking.\n");
	if (!has_read && has_write && !has_delete)
		fprintf(stdout, "writing.\n");
	if (!has_read && !has_write && has_delete)
		fprintf(stdout, "nuking.\n");
	if (!has_read && !has_write && !has_delete)
		fprintf(stdout, "nothing.\n");
	exit(0);

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
	double maxtime = 1.0;
	char * passphrase;
	int print_key_id = 0;
	int print_key_permissions = 0;

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
				    (keynum < 0) || (keynum > 31)) {
					warn0("Not a valid key number: %s",
					    tok);
					exit(1);
				}
				keyswanted |= (uint32_t)(1) << keynum;
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
		} else if (strcmp(argv[0], "--passphrase-time") == 0) {
			if ((maxtime != 1.0) || (argc < 2))
				usage();
			maxtime = strtod(argv[1], NULL);
			if ((maxtime < 0.05) || (maxtime > 86400)) {
				warn0("Invalid --passphrase-time argument: %s",
				    argv[1]);
				exit(1);
			}
			argv++; argc--;
		} else if (strcmp(argv[0], "--passphrased") == 0) {
			passphrased = 1;
		} else if (strcmp(argv[0], "--print-key-id") == 0) {
			print_key_id = 1;
		} else if (strcmp(argv[0], "--print-key-permissions") == 0) {
			print_key_permissions = 1;
		} else {
			/* Key files follow. */
			break;
		}
	}

	/* We can't print ID and permissions at the same time. */
	if (print_key_id && print_key_permissions)
		usage();

	if ((print_key_id || print_key_permissions)) {
		/* We can't combine printing info with generating a new key. */
		if (newkeyfile != NULL)
			usage();

		/* We can only print one at once. */
		if (argc != 1)
			usage();

		/* Print info. */
		if (print_key_id)
			print_id(argv[0]);
		if (print_key_permissions)
			print_permissions(argv[0]);
	}

	/* We should have an output key file. */
	if (newkeyfile == NULL)
		usage();

	/*
	 * It doesn't make sense to specify --passphrase-mem or
	 * --passphrase-time if we're not using a passphrase.
	 */
	if (((maxmem != 0) || (maxtime != 1.0)) && (passphrased == 0))
		usage();

	/* Warn the user if they're being silly. */
	if (keyswanted == 0) {
		warn0("None of {-r, -w, -d, --nuke} options are specified."
		    "  This will create a key file with no keys, which is"
		    " probably not what you intended.");
	}

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
	    passphrase, maxmem, maxtime))
		exit(1);

	/* Success! */
	return (0);
}
