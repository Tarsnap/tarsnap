#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "getopt.h"
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
	const char * print_key_id_file = NULL;
	const char * print_key_permissions_file = NULL;
	const char * ch;
	char * optarg_copy;	/* for strtok_r. */

	WARNP_INIT;

	/* Initialize key cache. */
	if (crypto_keys_init()) {
		warnp("Key cache initialization failed");
		exit(1);
	}

	/* Parse arguments. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("--outkeyfile"):
			if (newkeyfile != NULL)
				usage();
			newkeyfile = optarg;
			break;
		GETOPT_OPT("-r"):
			keyswanted |= CRYPTO_KEYMASK_READ;
			break;
		GETOPT_OPT("-w"):
			keyswanted |= CRYPTO_KEYMASK_WRITE;
			break;
		GETOPT_OPT("-d"):
			/*
			 * Deleting data requires both delete authorization
			 * and being able to read archives -- we need to be
			 * able to figure out which bits are part of the
			 * archive.
			 */
			keyswanted |= CRYPTO_KEYMASK_READ;
			keyswanted |= CRYPTO_KEYMASK_AUTH_DELETE;
			break;
		GETOPT_OPT("--nuke"):
			keyswanted |= CRYPTO_KEYMASK_AUTH_DELETE;
			break;
		GETOPT_OPTARG("--keylist"):
			/*
			 * This is a deliberately undocumented option used
			 * mostly for testing purposes; it allows a list of
			 * keys to be specified according to their numbers in
			 * crypto/crypto.h instead of using the predefined
			 * sets of "read", "write" and "delete" keys.
			 */
			if ((optarg_copy = strdup(optarg)) == NULL) {
				warn0("Out of memory");
				exit(0);
			}
			for (tok = strtok_r(optarg_copy, ",", &brkb);
			     tok;
			     tok = strtok_r(NULL, ",", &brkb)) {
				keynum = strtol(tok, &eptr, 0);
				if ((eptr == tok) ||
				    (keynum < 0) || (keynum > 31)) {
					warn0("Not a valid key number: %s",
					    tok);
					free(optarg_copy);
					exit(1);
				}
				keyswanted |= (uint32_t)(1) << keynum;
			}
			free(optarg_copy);
			break;
		GETOPT_OPTARG("--passphrase-mem"):
			if (maxmem != 0)
				usage();
			if (humansize_parse(optarg, &maxmem)) {
				warnp("Cannot parse --passphrase-mem"
				    " argument: %s", optarg);
				exit(1);
			}
			break;
		GETOPT_OPTARG("--passphrase-time"):
			if (maxtime != 1.0)
				usage();
			maxtime = strtod(optarg, NULL);
			if ((maxtime < 0.05) || (maxtime > 86400)) {
				warn0("Invalid --passphrase-time argument: %s",
				    optarg);
				exit(1);
			}
			break;
		GETOPT_OPT("--passphrased"):
			if (passphrased != 0)
				usage();
			passphrased = 1;
			break;
		GETOPT_OPTARG("--print-key-id"):
			if (print_key_id_file != NULL)
				usage();
			print_key_id_file = optarg;
			break;
		GETOPT_OPTARG("--print-key-permissions"):
			if (print_key_permissions_file != NULL)
				usage();
			print_key_permissions_file = optarg;
			break;
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s\n", ch);
			/* FALLTHROUGH */
		GETOPT_DEFAULT:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We can't print ID and permissions at the same time. */
	if ((print_key_id_file != NULL) && (print_key_permissions_file != NULL))
		usage();

	if ((print_key_id_file != NULL) ||
	    (print_key_permissions_file != NULL)) {
		/* We can't combine printing info with generating a new key. */
		if (newkeyfile != NULL)
			usage();

		/* We should have processed all arguments. */
		if (argc != 0)
			usage();

		/* Print info. */
		if (print_key_id_file != NULL)
			print_id(print_key_id_file);
		if (print_key_permissions_file != NULL)
			print_permissions(print_key_permissions_file);
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
