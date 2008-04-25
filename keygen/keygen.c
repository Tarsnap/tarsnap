#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "crypto.h"
#include "netpacket.h"
#include "netproto.h"
#include "network.h"
#include "sysendian.h"
#include "warnp.h"

/* Length of buffer for reading password. */
#define IBUFLEN	2048

struct register_internal {
	/* Parameters provided from main() to network code. */
	const char * user;
	const char * passwd;
	const char * name;

	/* State information. */
	int donechallenge;
	int done;

	/* Key used to send challenge response and verify server response. */
	uint8_t register_key[32];

	/* Data returned by server. */
	uint8_t status;
	uint64_t machinenum;
};

static sendpacket_callback callback_register_send;
static handlepacket_callback callback_register_challenge;
static handlepacket_callback callback_register_response;
static void usage(void);

static void
usage(void)
{

	fprintf(stderr, "usage: tarsnap-keygen %s %s %s\n",
	    "--keyfile key-file", "--user user-name",
	    "--machine machine-name");
	exit(1);

	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	struct register_internal C;
	const char * keyfilename;
	FILE * keyfile;
	struct termios term, term_old;
	int notatty = 0;
	char passbuf[IBUFLEN];
	uint8_t * keybuf;
	size_t keybuflen;
	uint8_t machinenum[8];
	NETPACKET_CONNECTION * NPC;

	/* We have no username, machine name, or key filename yet. */
	C.user = C.name = NULL;
	keyfilename = NULL;

	/* The password will be stored in passbuf. */
	C.passwd = passbuf;

	/* Parse arguments. */
	while (--argc) {
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
		} else {
			usage();
		}
	}

	/* We must have a user name, machine name, and key file specified. */
	if ((C.user == NULL) || (C.name == NULL) || (keyfilename == NULL))
		usage();

	/* Sanity-check the user name. */
	if (strlen(C.user) > 255) {
		fprintf(stderr, "User name too long: %s\n", C.user);
		exit(1);
	}

	if (strlen(C.name) > 255) {
		fprintf(stderr, "Machine name too long: %s\n", C.name);
		exit(1);
	}

	/* If stdin is a terminal, try to disable echo. */
	if (tcgetattr(STDIN_FILENO, &term_old) == 0) {
		memcpy(&term, &term_old, sizeof(struct termios));
		term.c_lflag = (term.c_lflag & ~ECHO) | ECHONL;
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);
	} else {
		notatty = 1;
	}

	/* If stdin is a terminal, prompt the user to enter the password */
	if (notatty == 0)
		fprintf(stderr, "Enter password: ");

	/* Read the password. */
	if (fgets(passbuf, IBUFLEN, stdin) == NULL) {
		if (notatty == 0)
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_old);
		warnp("fgets()");
		exit(1);
	}

	/* Terminate the string at the first "\r" or "\n" (if any). */
	passbuf[strcspn(passbuf, "\r\n")] = '\0';

	/* Reset the terminal if appropriate. */
	if (notatty == 0)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_old);

	/*
	 * Create key file -- we do this now rather than later so that we
	 * avoid registering with the server if we won't be able to create
	 * the key file later.
	 */
	if ((keyfile = fopen(keyfilename, "w")) == NULL) {
		warnp("Cannot create %s", keyfilename);
		exit(1);
	}

	/* Set the permissions on the key file to 0600. */
	if (fchmod(fileno(keyfile), S_IRUSR | S_IWUSR)) {
		warnp("fchmod(%s)", keyfilename);
		exit(1);
	}

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

	/* Generate keys. */
	if (crypto_keys_generate(CRYPTO_KEYMASK_USER)) {
		warnp("Error generating keys");
		exit(1);
	}

	/* Initialize network layer. */
	if (network_init()) {
		warnp("Network layer initialization failed");
		exit(1);
	}

	/*
	 * We're not done, haven't answered a challenge, and don't have a
	 * machine number.
	 */
	C.done = 0;
	C.donechallenge = 0;
	C.machinenum = (uint64_t)(-1);

	/* Open netpacket connection. */
	if ((NPC = netpacket_open()) == NULL)
		goto err1;

	/* Ask the netpacket layer to send a request and get a response. */
	if (netpacket_op(NPC, callback_register_send, &C))
		goto err1;

	/* Run event loop until an error occurs or we're done. */
	if (network_spin(&C.done))
		goto err1;

	/* Close netpacket connection. */
	if (netpacket_close(NPC))
		goto err1;

	/*
	 * If we didn't respond to a challenge, the server's response must
	 * have been a "no such user" error.
	 */
	if ((C.donechallenge == 0) && (C.status != 1)) {
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		exit(1);
	}

	/* The machine number should be -1 iff the status is nonzero. */
	if (((C.machinenum == (uint64_t)(-1)) && (C.status == 0)) ||
	    ((C.machinenum != (uint64_t)(-1)) && (C.status != 0))) {
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		exit(1);
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
	default:
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		warnp("Error registering with server");
		exit(1);
	}

	/* Shut down the network event loop. */
	network_fini();

	/* Exit with a code of 1 if we couldn't register. */
	if (C.machinenum == (uint64_t)(-1))
		exit(1);

	/* Export keys. */
	if (crypto_keys_export(CRYPTO_KEYMASK_USER, &keybuf, &keybuflen)) {
		warnp("Error exporting keys");
		exit(1);
	}

	/* Write keys. */
	be64enc(machinenum, C.machinenum);
	if (fwrite(machinenum, 8, 1, keyfile) != 1) {
		warnp("Error writing keys");
		exit(1);
	}
	if (fwrite(keybuf, keybuflen, 1, keyfile) != 1) {
		warnp("Error writing keys");
		exit(1);
	}

	/* Close the key file. */
	if (fclose(keyfile)) {
		warnp("Error closing key file");
		exit(1);
	}

	/* Success! */
	return (0);

err1:
	warnp("Error registering with server");
	exit(1);
}

static int
callback_register_send(void * cookie, NETPACKET_CONNECTION * NPC)
{
	struct register_internal * C = cookie;

	/* Tell the server which user is trying to add a machine. */
	return (netpacket_register_request(NPC, C->user,
	    callback_register_challenge));
}

static int
callback_register_challenge(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct register_internal * C = cookie;
	uint8_t pub[CRYPTO_DH_PUBLEN];
	uint8_t priv[CRYPTO_DH_PRIVLEN];
	uint8_t K[CRYPTO_DH_KEYLEN];
	uint8_t keys[96];

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/*
	 * Make sure we received the right type of packet.  It is legal for
	 * the server to send back a NETPACKET_REGISTER_RESPONSE at this
	 * point; call callback_register_response to handle those.
	 */
	if (packettype == NETPACKET_REGISTER_RESPONSE)
		return (callback_register_response(cookie, NPC, status,
		    packettype, packetbuf, packetlen));
	else if (packettype != NETPACKET_REGISTER_CHALLENGE) {
		netproto_printerr(NETPROTO_STATUS_PROTERR);
		goto err0;
	}

	/* Generate DH parameters from the password and salt. */
	if (crypto_passwd_to_dh(C->passwd, packetbuf, pub, priv)) {
		warnp("Could not generate DH parameter from password");
		goto err0;
	}

	/* Compute shared key. */
	if (crypto_dh_compute(&packetbuf[32], priv, K))
		goto err0;
	if (crypto_hash_data(CRYPTO_KEY_HMAC_SHA256, K, CRYPTO_DH_KEYLEN,
	    C->register_key)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		goto err0;
	}

	/* Export access keys. */
	if (crypto_keys_raw_export_auth(keys))
		goto err0;

	/* Send challenge response packet. */
	if (netpacket_register_cha_response(NPC, keys, C->name,
	    C->register_key, callback_register_response))
		goto err0;

	/* We've responded to a challenge. */
	C->donechallenge = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_register_response(void * cookie, NETPACKET_CONNECTION * NPC,
    int status, uint8_t packettype, const uint8_t * packetbuf,
    size_t packetlen)
{
	struct register_internal * C = cookie;
	uint8_t hmac_actual[32];

	(void)NPC; /* UNUSED */
	(void)packetlen; /* UNUSED */

	/* Handle errors. */
	if (status != NETWORK_STATUS_OK) {
		netproto_printerr(status);
		goto err0;
	}

	/* Make sure we received the right type of packet. */
	if (packettype != NETPACKET_REGISTER_RESPONSE)
		goto err1;

	/* Verify packet hmac. */
	if (packetbuf[0] == 0) {
		crypto_hash_data_key_2(C->register_key, 32, &packettype, 1,
		    packetbuf, 9, hmac_actual);
	} else {
		memset(hmac_actual, 0, 32);
	}
	if (crypto_verify_bytes(hmac_actual, &packetbuf[9], 32))
		goto err1;

	/* Record status code and machine number returned by server. */
	C->status = packetbuf[0];
	C->machinenum = be64dec(&packetbuf[1]);

	/* We have received a response. */
	C->done = 1;

	/* Success! */
	return (0);

err1:
	netproto_printerr(NETPROTO_STATUS_PROTERR);
err0:
	/* Failure! */
	return (-1);
}
