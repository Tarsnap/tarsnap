#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdint.h>	/* uint8_t, uint64_t */
#include <unistd.h>	/* size_t */

#include "crypto.h"

#define STORAGE_FILE_OVERHEAD	(CRYPTO_FILE_HLEN + CRYPTO_FILE_TLEN)

typedef struct storage_read_internal	STORAGE_R;
typedef struct storage_write_internal	STORAGE_W;
typedef struct storage_delete_internal	STORAGE_D;

/**
 * storage_read_init(machinenum):
 * Prepare for read operations.  Note that since reads are non-transactional,
 * this could be a no-op aside from storing the machine number.
 */
STORAGE_R * storage_read_init(uint64_t);

/**
 * storage_read_cache(S, class, name):
 * Add the file ${name} from class ${class} into the cache for the read cookie
 * ${S} returned from storage_read_init.  The data will not be fetched yet;
 * but any future fetch will look in the cache first and will store the block
 * in the cache if it needs to be fetched.
 */
int storage_read_cache(STORAGE_R *, char, const uint8_t[32]);

/**
 * storage_read_set_cache_limit(S, size):
 * Set a limit of ${size} bytes on the cache associated with read cookie ${S}.
 */
void storage_read_set_cache_limit(STORAGE_R *, size_t);

/**
 * storage_read_file(S, buf, buflen, class, name):
 * Read the file ${name} from class ${class} using the read cookie ${S}
 * returned from storage_read_init into the buffer ${buf} of length ${buflen}.
 * Return 0 on success, 1 if the file does not exist; 2 if the file is not
 * ${buflen} bytes long or is corrupt; or -1 on error.
 */
int storage_read_file(STORAGE_R *, uint8_t *, size_t, char,
    const uint8_t[32]);

/**
 * storage_read_file_alloc(S, buf, buflen, class, name):
 * Allocate a buffer and read the file ${name} from class ${class} using the
 * read cookie ${S} into it; set ${buf} to point at the buffer, and
 * ${buflen} to the length of the buffer.  Return 0, 1, 2, or -1 as per
 * storage_read_file.
 */
int storage_read_file_alloc(STORAGE_R *, uint8_t **, size_t *, char,
    const uint8_t[32]);

/**
 * storage_read_file_callback(S, buf, buflen, class, name, callback, cookie):
 * Read the file ${name} from class ${class} using the read cookie ${S}
 * returned from storage_read_init.  If ${buf} is non-NULL, then read the
 * file (which should be ${buflen} bytes in length) into ${buf}; otherwise
 * malloc a buffer.  Invoke ${callback}(${cookie}, status, b, blen) when
 * complete, where ${status} is 0, 1, 2, or -1 as per storage_read_file,
 * ${b} is the buffer into which the data was read (which will be ${buf} if
 * that value was non-NULL) and ${blen} is the length of the file.
 */
int storage_read_file_callback(STORAGE_R *, uint8_t *, size_t, char,
    const uint8_t[32], int(*)(void *, int, uint8_t *, size_t), void *);

/**
 * storage_read_free(S):
 * Close the read cookie ${S} and free any allocated memory.
 */
void storage_read_free(STORAGE_R *);

/**
 * storage_write_start(machinenum, lastseq, seqnum):
 * Start a write transaction, presuming that ${lastseq} is the sequence
 * number of the last committed transaction, or zeroes if there is no
 * previous transaction; and store the sequence number of the new transaction
 * into ${seqnum}.
 */
STORAGE_W * storage_write_start(uint64_t, const uint8_t[32], uint8_t[32]);

/**
 * storage_write_fexist(S, class, name):
 * Test if a file ${name} exists in class ${class}, as part of the write
 * transaction associated with the cookie ${S}; return 1 if the file
 * exists, 0 if not, and -1 on error.  If ${S} is NULL, return 0 without doing
 * anything.
 */
int storage_write_fexist(STORAGE_W *, char, const uint8_t[32]);

/**
 * storage_write_file(S, buf, len, class, name):
 * Write ${len} bytes from ${buf} to the file ${name} in class ${class} as
 * part of the write transaction associated with the cookie ${S}.  If ${S} is
 * NULL, return 0 without doing anything.
 */
int storage_write_file(STORAGE_W *, uint8_t *, size_t, char,
    const uint8_t[32]);

/**
 * storage_write_flush(S):
 * Make sure all files written as part of the transaction associated with
 * the cookie ${S} have been safely stored in preparation for being committed.
 * If ${S} is NULL, return 0 without doing anything.
 */
int storage_write_flush(STORAGE_W *);

/**
 * storage_write_end(S):
 * Make sure all files written as part of the transaction associated with
 * the cookie ${S} have been safely stored in preparation for being
 * committed; and close the transaction and free associated memory.  If ${S}
 * is NULL, return 0 without doing anything.
 */
int storage_write_end(STORAGE_W *);

/**
 * storage_write_free(S):
 * Free any memory allocated as part of the write transaction associated with
 * the cookie ${S}; the transaction will not be committed.
 */
void storage_write_free(STORAGE_W *);

/**
 * storage_delete_start(machinenum, lastseq, seqnum):
 * Start a delete transaction, presuming that ${lastseq} is the sequence
 * number of the last committed transaction, or zeroes if there is no
 * previous transaction; and store the sequence number of the new transaction
 * into ${seqnum}.
 */
STORAGE_D * storage_delete_start(uint64_t, const uint8_t[32], uint8_t[32]);

/**
 * storage_fsck_start(machinenum, seqnum, readonly, whichkey):
 * Start a fsck transaction, and store the sequence number of said
 * transaction into ${seqnum}.  If ${whichkey} is zero, use the write key
 * (in which case the transaction must be readonly).
 */
STORAGE_D * storage_fsck_start(uint64_t, uint8_t[32], int, uint8_t);

/**
 * storage_delete_file(S, class, name):
 * Delete the file ${name} from class ${class} as part of the delete
 * transaction associated with the cookie ${S}.
 */
int storage_delete_file(STORAGE_D *, char, const uint8_t[32]);

/**
 * storage_delete_flush(S):
 * Make sure all operations performed as part of the transaction associated
 * with the cookie ${S} have been safely stored in preparation for being
 * committed.
 */
int storage_delete_flush(STORAGE_D *);

/**
 * storage_delete_end(S):
 * Make sure that all operations performed as part of the transaction
 * associated with the cookie ${S} have been safely stored in
 * preparation for being committed; and close the transaction and free
 * associated memory.
 */
int storage_delete_end(STORAGE_D *);

/**
 * storage_delete_free(S):
 * Free any memory allocated as part of the delete transaction associated
 * with the cookie ${S}; the transaction will not be committed.
 */
void storage_delete_free(STORAGE_D *);

/**
 * storage_transaction_checkpoint(machinenum, seqnum, ckptnonce, whichkey):
 * Create a checkpoint ${ckptnonce} in the current write transaction, which
 * has nonce ${seqnum}.  The value ${whichkey} is defined as in
 * storage_transaction_commit.
 */
int storage_transaction_checkpoint(uint64_t, const uint8_t[32],
    const uint8_t[32], uint8_t);

/**
 * storage_transaction_commit(machinenum, seqnum, whichkey):
 * Commit the transaction ${seqnum} if it is the most recent uncommitted
 * transaction.  The value ${whichkey} specifies a key which should be used
 * to sign the commit request: 0 if the write key should be used, and 1 if
 * the delete key should be used.
 */
int storage_transaction_commit(uint64_t, const uint8_t[32], uint8_t);

/**
 * storage_transaction_commitfromcheckpoint(machinenum, whichkey):
 * If a write transaction is currently in progress and has a checkpoint,
 * commit it.  The value ${whichkey} specifies a key which should be used
 * to sign the commit request: 0 if the write key should be used, and 1 if
 * the delete key should be used.
 */
int storage_transaction_commitfromcheckpoint(uint64_t, uint8_t);

/**
 * storage_directory_read(machinenum, class, key, flist, nfiles):
 * Fetch a sorted list of files in the specified class.  If ${key} is 0, use
 * NETPACKET_DIRECTORY requests (using the read key); otherwise, use
 * NETPACKET_DIRECTORY_D requests (using the delete key).  Return the list
 * and the number of files via ${flist} and ${nfiles} respectively.
 */
int storage_directory_read(uint64_t, char, int, uint8_t **, size_t *);

#endif /* !_STORAGE_H_ */
