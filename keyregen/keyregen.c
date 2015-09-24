#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "crypto.h"
#include "crypto_dh.h"
#include "crypto_verify_bytes.h"
#include "humansize.h"
#include "keyfile.h"
#include "keygen.h"
#include "netpacket.h"
#include "netproto.h"
#include "tsnetwork.h"
#include "readpass.h"
#include "sysendian.h"
#include "tarsnap_opt.h"
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
	exit(1);

	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	struct register_internal C;
	const char * keyfilename;
	const char * oldkeyfilename;
	FILE * keyfile;
	NETPACKET_CONNECTION * NPC;
	int passphrased;
	uint64_t maxmem;
	double maxtime;
	char * passphrase;
	uint64_t dummy;

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
		} else if (strcmp(argv[0], "--oldkey") == 0) {
			if ((oldkeyfilename != NULL) || (argc < 2))
				usage();
			oldkeyfilename = argv[1];
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

	/* Sanity-check the user name. */
	if (strlen(C.user) > 255) {
		fprintf(stderr, "User name too long: %s\n", C.user);
		exit(1);
	}
	if (strlen(C.user) == 0) {
		fprintf(stderr, "User name must be non-empty\n");
		exit(1);
	}

	/* Sanity-check the machine name. */
	if (strlen(C.name) > 255) {
		fprintf(stderr, "Machine name too long: %s\n", C.name);
		exit(1);
	}
	if (strlen(C.name) == 0) {
		fprintf(stderr, "Machine name must be non-empty\n");
		exit(1);
	}

	/* Get a password. */
	if (readpass(&C.passwd, "Enter tarsnap account password", NULL, 0)) {
		warnp("Error reading password");
		exit(1);
	}

	/*
	 * Create key file -- we do this now rather than later so that we
	 * avoid registering with the server if we won't be able to create
	 * the key file later.
	 */
	if ((keyfile = keyfile_write_open(keyfilename)) == NULL) {
		warnp("Cannot create %s", keyfilename);
		exit(1);
	}

	/* Initialize key cache. */
	if (crypto_keys_init()) {
		warnp("Key cache initialization failed");
		goto err1;
	}

	/*
	 * Load the keys CRYPTO_KEY_HMAC_{CHUNK, NAME, CPARAMS} from the old
	 * key file, since these are the keys which need to be consistent in
	 * order for two key sets to be compatible.  (CHUNK and NAME are used
	 * to compute the 32-byte keys for blocks; CPARAMS is used to compute
	 * parameters used to split a stream of bytes into chunks.)
	 */
	if (keyfile_read(oldkeyfilename, &dummy,
	    CRYPTO_KEYMASK_HMAC_CHUNK |
	    CRYPTO_KEYMASK_HMAC_NAME |
	    CRYPTO_KEYMASK_HMAC_CPARAMS)) {
		warnp("Error reading old key file");
		goto err1;
	}

	/*
	 * Generate all of the user keys except for the ones which we loaded
	 * from the old keyfile.
	 */
	if (crypto_keys_generate(CRYPTO_KEYMASK_USER &
	    ~CRYPTO_KEYMASK_HMAC_CHUNK &
	    ~CRYPTO_KEYMASK_HMAC_NAME &
	    ~CRYPTO_KEYMASK_HMAC_CPARAMS)) {
		warnp("Error generating keys");
		goto err1;
	}

	/*
	 * We're not done, haven't answered a challenge, and don't have a
	 * machine number.
	 */
	C.done = 0;
	C.donechallenge = 0;
	C.machinenum = (uint64_t)(-1);

	/* Open netpacket connection. */
	if ((NPC = netpacket_open(USERAGENT)) == NULL)
		goto err2;

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(NPC, callback_register_send, &C))
		goto err2;

	/* Run event loop until an error occurs or we're done. */
	if (network_spin(&C.done))
		goto err2;

	/* Close netpacket connection. */
	if (netpacket_close(NPC))
		goto err2;

	/*
	 * If we didn't respond to a challenge, the server's response must
	 * have been a "no such user" error.
	 */
	if ((C.donechallenge == 0) && (C.status != 1)) {
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err1;
	}

	/* The machine number should be -1 iff the status is nonzero. */
	if (((C.machinenum == (uint64_t)(-1)) && (C.status == 0)) ||
	    ((C.machinenum != (uint64_t)(-1)) && (C.status != 0))) {
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err1;
	}

	/* Parse status returned by server. */
	switch (C.status) {
	case 0:
		/* Success! */
		break;
	case 1:
		warn0("No such user: %s", C.user);
		break;
	case 2:
		warn0("Incorrect password");
		break;
	case 3:
		warn0("Cannot register with server: "
		    "Account balance for user %s is not positive", C.user);
		break;
	default:
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err2;
	}

	/* Shut down the network event loop. */
	network_fini();

	/* Exit with a code of 1 if we couldn't register. */
	if (C.machinenum == (uint64_t)(-1))
		goto err1;

	/* If the user wants to passphrase the keyfile, get the passphrase. */
	if (passphrased != 0) {
		if (readpass(&passphrase,
		    "Please enter passphrase for keyfile encryption",
		    "Please confirm passphrase for keyfile encryption", 1)) {
			warnp("Error reading password");
			goto err1;
		}
	} else {
		passphrase = NULL;
	}

	/* Write keys to file. */
	if (keyfile_write_file(keyfile, C.machinenum,
	    CRYPTO_KEYMASK_USER, passphrase, maxmem, maxtime))
		goto err1;

	/* Close the key file. */
	if (fclose(keyfile)) {
		warnp("Error closing key file");
		goto err1;
	}

	/* Success! */
	return (0);

err2:
	warnp("Error registering with server");
err1:
	unlink(keyfilename);
	exit(1);
}

