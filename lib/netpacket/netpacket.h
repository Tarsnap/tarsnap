#ifndef _NETPACKET_H_
#define _NETPACKET_H_

#include <stdint.h>

#include "crypto.h"
#include "netproto.h"

/* Internal netpacket cookie. */
typedef struct netpacket_internal NETPACKET_CONNECTION;

/* Function for sending a request once a connection is established. */
typedef int sendpacket_callback(void *, NETPACKET_CONNECTION *);

/* Function for handling a response packet. */
typedef int handlepacket_callback(void *, NETPACKET_CONNECTION *,
    int, uint8_t, const uint8_t *, size_t);

/* packet types. */
#define NETPACKET_REGISTER_REQUEST		0x00
#define NETPACKET_REGISTER_CHALLENGE		0x80
#define NETPACKET_REGISTER_CHA_RESPONSE		0x01
#define NETPACKET_REGISTER_RESPONSE		0x81
#define NETPACKET_TRANSACTION_GETNONCE		0x10
#define NETPACKET_TRANSACTION_GETNONCE_RESPONSE	0x90
#define NETPACKET_TRANSACTION_START		0x11
#define NETPACKET_TRANSACTION_START_RESPONSE	0x91
#define NETPACKET_TRANSACTION_COMMIT		0x12
#define NETPACKET_TRANSACTION_COMMIT_RESPONSE	0x92
#define NETPACKET_TRANSACTION_CHECKPOINT	0x13
#define NETPACKET_TRANSACTION_CHECKPOINT_RESPONSE 0x93
#define NETPACKET_TRANSACTION_CANCEL		0x14
#define NETPACKET_TRANSACTION_CANCEL_RESPONSE	0x94
#define NETPACKET_TRANSACTION_TRYCOMMIT		0x15
#define NETPACKET_TRANSACTION_TRYCOMMIT_RESPONSE 0x95
#define NETPACKET_TRANSACTION_ISCHECKPOINTED	0x16
#define NETPACKET_TRANSACTION_ISCHECKPOINTED_RESPONSE 0x96
#define NETPACKET_WRITE_FEXIST			0x20
#define NETPACKET_WRITE_FEXIST_RESPONSE		0xa0
#define NETPACKET_WRITE_FILE			0x21
#define NETPACKET_WRITE_FILE_RESPONSE		0xa1
#define NETPACKET_DELETE_FILE			0x30
#define NETPACKET_DELETE_FILE_RESPONSE		0xb0
#define NETPACKET_READ_FILE			0x40
#define NETPACKET_READ_FILE_RESPONSE		0xc0
#define NETPACKET_DIRECTORY			0x50
#define NETPACKET_DIRECTORY_D			0x51
#define NETPACKET_DIRECTORY_RESPONSE		0xd0

/* Maximum number of files listed in a NETPACKET_DIRECTORY_RESPONSE packet. */
#define NETPACKET_DIRECTORY_RESPONSE_MAXFILES	8000

/**
 * netpacket_hmac_verify(type, nonce, packetbuf, pos, key):
 * Verify that HMAC(type || nonce || packetbuf[0 .. pos - 1]) using the
 * specified key matches packetbuf[pos .. pos + 31].  If nonce is NULL, omit
 * it from the data being HMACed as appropriate.  Return -1 on error, 0 on
 * success, or 1 if the hash does not match.
 */
int netpacket_hmac_verify(uint8_t, const uint8_t[32], const uint8_t *,
    size_t, int);

/**
 * netpacket_register_request(NPC, user, callback):
 * Construct and send a NETPACKET_REGISTER_REQUEST packet asking to register
 * a new machine belonging to the specified user.
 */
int netpacket_register_request(NETPACKET_CONNECTION *, const char *,
    handlepacket_callback *);

/**
 * netpacket_register_cha_response(NPC, keys, name, register_key, callback):
 * Construct and send a NETPACKET_REGISTER_CHA_RESPONSE packet providing the
 * given access keys and user-friendly name, signed using the shared key
 * ${register_key} computed by hashing the Diffie-Hellman shared secret K.
 */
int netpacket_register_cha_response(NETPACKET_CONNECTION *,
    const uint8_t[96], const char *, const uint8_t[32],
    handlepacket_callback *);

/**
 * netpacket_transaction_getnonce(NPC, machinenum, callback):
 * Construct and send a NETPACKET_TRANSACTION_GETNONCE packet asking to get
 * a transaction server nonce.
 */
int netpacket_transaction_getnonce(NETPACKET_CONNECTION *, uint64_t,
    handlepacket_callback *);

/**
 * netpacket_transaction_start(NPC, machinenum, operation, snonce, cnonce,
 *     state, callback):
 * Construct and send a NETPACKET_TRANSACTION_START packet asking to
 * start a transaction; the transaction is a write transaction if
 * ${operation} is 0, a delete transaction if ${operation} is 1, or a fsck
 * transaction if ${operation} is 2.
 */
int netpacket_transaction_start(NETPACKET_CONNECTION *, uint64_t,
    uint8_t, const uint8_t[32], const uint8_t[32], const uint8_t[32],
    handlepacket_callback *);

/**
 * netpacket_transaction_commit(NPC, machinenum, whichkey, nonce, callback):
 * Construct and send a NETPACKET_TRANSACTION_COMMIT packet asking to commit
 * a transaction; the packet is signed with the write access key if
 * ${whichkey} is 0, and with the delete access key if ${whichkey} is 1.
 */
int netpacket_transaction_commit(NETPACKET_CONNECTION *, uint64_t,
    uint8_t, const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_transaction_checkpoint(NPC, machinenum, whichkey, ckptnonce,
 *     nonce, callback):
 * Construct and send a NETPACKET_TRANSACTION_CHECKPOINT packet asking to
 * create a checkpoint in a write transaction.
 */
int netpacket_transaction_checkpoint(NETPACKET_CONNECTION *, uint64_t,
    uint8_t, const uint8_t[32], const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_transaction_cancel(NPC, machinenum, whichkey, snonce, cnonce,
 *     state, callback):
 * Construct and send a NETPACKET_TRANSACTION_CANCEL packet asking to cancel
 * a pending transaction if the state is correct.
 */
int netpacket_transaction_cancel(NETPACKET_CONNECTION *, uint64_t,
    uint8_t, const uint8_t[32], const uint8_t[32], const uint8_t[32],
    handlepacket_callback *);

/**
 * netpacket_transaction_trycommit(NPC, machinenum, whichkey, nonce,
 *     callback):
 * Construct and send a NETPACKET_TRANSACTION_TRYCOMMIT packet asking to
 * commit a transaction; the packet is signed with the write access key if
 * ${whichkey} is 0, and with the delete access key if ${whichkey} is 1.
 */
int netpacket_transaction_trycommit(NETPACKET_CONNECTION *, uint64_t,
    uint8_t, const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_transaction_ischeckpointed(NPC, machinenum, whichkey, nonce,
 *     callback):
 * Construct and send a NETPACKET_TRANSACTION_ISCHECKPOINTED packet asking if
 * a checkpointed write transaction is in progress; the packet is signed with
 * the write access key if ${whichkey} is 0, and with the delete access key
 * if ${whichkey} is 1.
 */
int netpacket_transaction_ischeckpointed(NETPACKET_CONNECTION *, uint64_t,
    uint8_t, const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_write_fexist(NPC, machinenum, class, name, nonce, callback):
 * Construct and send a NETPACKET_WRITE_FEXIST packet asking if the
 * specified file exists.
 */
int netpacket_write_fexist(NETPACKET_CONNECTION *, uint64_t, uint8_t,
    const uint8_t[32], const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_write_file(NPC, machinenum, class, name, buf, buflen,
 *     nonce, callback):
 * Construct and send a NETPACKET_WRITE_FILE packet asking to write the
 * specified file.
 */
int netpacket_write_file(NETPACKET_CONNECTION *, uint64_t, uint8_t,
    const uint8_t[32], const uint8_t *, size_t,
    const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_delete_file(NPC, machinenum, class, name, nonce, callback):
 * Construct and send a NETPACKET_DELETE_FILE packet asking to delete the
 * specified file.
 */
int netpacket_delete_file(NETPACKET_CONNECTION *, uint64_t, uint8_t,
    const uint8_t[32], const uint8_t[32], handlepacket_callback *);

/**
 * netpacket_read_file(NPC, machinenum, class, name, size, callback):
 * Construct and send a NETPACKET_READ_FILE packet asking to read the
 * specified file, which should be ${size} (<= 262144) bytes long if ${size}
 * is not (uint32_t)(-1).
 */
int netpacket_read_file(NETPACKET_CONNECTION *, uint64_t, uint8_t,
    const uint8_t[32], uint32_t, handlepacket_callback *);

/**
 * netpacket_directory(NPC, machinenum, class, start, snonce, cnonce, key,
 *     callback):
 * Construct and send a NETPACKET_DIRECTORY packet (if key == 0) or
 * NETPACKET_DIRECTORY_D packet (otherwise) asking for a list of files
 * of the specified class starting from the specified position.
 */
int netpacket_directory(NETPACKET_CONNECTION *, uint64_t, uint8_t,
    const uint8_t[32], const uint8_t[32], const uint8_t[32], int,
    handlepacket_callback *);

/**
 * netpacket_directory_readmore(NPC, callback):
 * Read more NETPACKET_DIRECTORY_RESPONSE packets.
 */
int netpacket_directory_readmore(NETPACKET_CONNECTION *,
    handlepacket_callback *);

/**
 * netpacket_open(useragent):
 * Return a netpacket connection cookie.
 */
NETPACKET_CONNECTION * netpacket_open(const char *);

/**
 * netpacket_op(NPC, writepacket, cookie):
 * Call ${writepacket} to send a request to the server over the provided
 * netpacket connection.
 */
int netpacket_op(NETPACKET_CONNECTION *, sendpacket_callback *, void *);

/**
 * netpacket_getstats(NPC, in, out, queued):
 * Obtain the number of bytes received and sent via the connection, and the
 * number of bytes queued to be written.
 */
void netpacket_getstats(NETPACKET_CONNECTION *, uint64_t *, uint64_t *,
    uint64_t *);

/**
 * netpacket_close(NPC):
 * Close a netpacket connection.
 */
int netpacket_close(NETPACKET_CONNECTION *);

#endif /* !_NETPACKET_H_ */
