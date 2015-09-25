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

	fprintf(stderr, "usage: tarsnap-keygen %s %s %s %s %s %s\n",
	    "--keyfile key-file", "--user user-name",
	    "--machine machine-name",
	    "[--passphrased]", "[--passphrase-mem maxmem]",
	    "[--passphrase-time maxtime]");
	exit(1);

	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	struct register_internal C;
	const char * keyfilename;
	int passphrased;
	uint64_t maxmem;
	double maxtime;

	WARNP_INIT;

	/* We have no username, machine name, or key filename yet. */
	C.user = C.name = NULL;
	keyfilename = NULL;

	/*
	 * So far we're not using a passphrase, have unlimited RAM, and allow
	 * up to 1 second of CPU time.
	 */
	passphrased = 0;
	maxmem = 0;
	maxtime = 1.0;

	/* Parse arguments. */
	while (--argc > 0) {
		argv++;

		if (strcmp(argv[0], "--user") == 0) {
			if ((C.user != NULL) || (argc < 2))
				usage();
			C.user = argv[1];
			argv++; argc--;
		} else if (strcmp(argv[0], "--machine") == 0) {
			if ((C.name != NULL) || (argc < 2))
				usage();
			C.name = argv[1];
			argv++; argc--;
		} else if (strcmp(argv[0], "--keyfile") == 0) {
			if ((keyfilename != NULL) || (argc < 2))
				usage();
			keyfilename = argv[1];
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
		} else {
			usage();
		}
	}

	/* We must have a user name, machine name, and key file specified. */
	if ((C.user == NULL) || (C.name == NULL) || (keyfilename == NULL))
		usage();

	/*
	 * It doesn't make sense to specify --passphrase-mem or
	 * --passphrase-time if we're not using a passphrase.
	 */
	if (((maxmem != 0) || (maxtime != 1.0)) && (passphrased == 0))
		usage();

	/*
	 * Use shared code between keygen and keyregen for the actual
	 * processing.  The NULL parameter is the oldkeyfilename (used for
	 * keyregen)
	 */
	if (keygen_actual(&C, keyfilename, passphrased, maxmem, maxtime,
	    NULL) != 0)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (1);
}


