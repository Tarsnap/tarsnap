#include "bsdtar_platform.h"

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
#include "getopt.h"
#include "humansize.h"
#include "keyfile.h"
#include "keygen.h"
#include "readpass.h"
#include "sysendian.h"
#include "tarsnap_opt.h"
#include "tsnetwork.h"
#include "warnp.h"

static void usage(void);

/* Be noisy about network errors while registering a machine. */
int tarsnap_opt_noisy_warnings = 1;

static void
usage(void)
{

	fprintf(stderr, "usage: tarsnap-keyregen %s %s %s %s %s %s %s\n",
	    "--keyfile key-file", "--oldkey old-key-file",
	    "--user user-name", "--machine machine-name",
	    "[--passphrased]", "[--passphrase-mem maxmem]",
	    "[--passphrase-time maxtime]");
	fprintf(stderr, "       tarsnap-keyregen --version\n");
	exit(1);

	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	struct register_internal C;
	const char * keyfilename;
	const char * oldkeyfilename;
	int passphrased;
	uint64_t maxmem;
	double maxtime;
	const char * ch;

	WARNP_INIT;

	/*
	 * We have no username, machine name, key filename, or old key
	 * filename yet.
	 */
	C.user = C.name = NULL;
	keyfilename = NULL;
	oldkeyfilename = NULL;

	/*
	 * So far we're not using a passphrase, have unlimited RAM, and allow
	 * up to 1 second of CPU time.
	 */
	passphrased = 0;
	maxmem = 0;
	maxtime = 1.0;

	/* Parse arguments. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("--user"):
			if (C.user != NULL)
				usage();
			C.user = optarg;
			break;
		GETOPT_OPTARG("--machine"):
			if (C.name != NULL)
				usage();
			C.name = optarg;
			break;
		GETOPT_OPTARG("--keyfile"):
			if (keyfilename != NULL)
				usage();
			keyfilename = optarg;
			break;
		GETOPT_OPTARG("--oldkey"):
			if (oldkeyfilename != NULL)
				usage();
			oldkeyfilename = optarg;
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
		GETOPT_OPT("--version"):
			fprintf(stderr, "tarsnap-keyregen %s\n",
			    PACKAGE_VERSION);
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s\n", ch);
			/* FALLTHROUGH */
		GETOPT_DEFAULT:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have processed all the arguments. */
	if (argc != 0)
		usage();

	/*
	 * We must have a user name, machine name, key file, and old key
	 * file specified.
	 */
	if ((C.user == NULL) || (C.name == NULL) ||
	    (keyfilename == NULL) || (oldkeyfilename == NULL))
		usage();

	/*
	 * It doesn't make sense to specify --passphrase-mem or
	 * --passphrase-time if we're not using a passphrase.
	 */
	if (((maxmem != 0) || (maxtime != 1.0)) && (passphrased == 0))
		usage();

	/*
	 * Use shared code between keygen and keyregen for the actual
	 * processing.
	 */
	if (keygen_actual(&C, keyfilename, passphrased, maxmem, maxtime,
	    oldkeyfilename) != 0)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (1);
}

