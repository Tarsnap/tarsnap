#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <sys/types.h>	/* Linux is broken and requires this for off_t. */
#include <stdint.h>	/* uint8_t, uint64_t */
#include <unistd.h>	/* off_t, size_t */

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
 * storage_read_free(S):
 * Close the read cookie ${S} and free any allocated memory.
 */
void storage_read_free(STORAGE_R *);

/**
 * storage_write_start(machinenum, lastseq, seqnum, dryrun):
 * Start a write transaction, presuming that ${lastseq} is the the sequence
 * number of the last committed transaction, or zeroes if there is no
 * previous transaction; and store the sequence number of the new transaction
 * into ${seqnum}.  If ${dryrun} is nonzero, perform a dry run.
 */
STORAGE_W * storage_write_start(uint64_t, const uint8_t[32], uint8_t[32],
    int);

/**
 * storage_write_fexist(S, class, name):
 * Test if a file ${name} exists in class ${class}, as part of the write
 * transaction associated with the cookie ${S}; return 1 if the file
 * exists, 0 if not, and -1 on error.
 */
int storage_write_fexist(STORAGE_W *, char, const uint8_t[32]);

/**
 * storage_write_file(S, buf, len, class, name):
 * Write ${len} bytes from ${buf} to the file ${name} in class ${class} as
 * part of the write transaction associated with the cookie ${S}.
 */
int storage_write_file(STORAGE_W *, uint8_t *, size_t, char,
    const uint8_t[32]);

/**
 * storage_write_flush(S):
 * Make sure all files written as part of the transaction associated with
 * the cookie ${S} have been safely stored in preparation for being committed.
 */
int storage_write_flush(STORAGE_W *);

/**
 * storage_write_end(S):
 * Make sure all files written as part of the transaction associated with
 * the cookie ${S} have been safely stored in preparation for being
 * committed; and close the transaction and free associated memory.
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
 * Start a delete transaction, presuming that ${lastseq} is the the sequence
 * number of the last committed transaction, or zeroes if there is no
 * previous transaction; and store the sequence number of the new transaction
 * into ${seqnum}.
 */
STORAGE_D * storage_delete_start(uint64_t, const uint8_t[32], uint8_t[32]);

/**
 * storage_fsck_start(machinenum, seqnum):
 * Start a fsck transaction, and store the sequence number of said
 * transaction into ${seqnum}.
 */
STORAGE_D * storage_fsck_start(uint64_t, uint8_t[32]);

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
 * Free any memory allocated as part of the delete transcation associated
 * with the cookie ${S}; the transaction will not be committed.
 */
void storage_delete_free(STORAGE_D *);

/**
 * storage_transaction_commit(machinenum, seqnum, whichkey):
 * Commit the transaction ${seqnum} if it is the most recent uncommitted
 * transaction.  The value ${whichkey} specifies a key which should be used
 * to sign the commit request: 0 if the write key should be used, and 1 if
 * the delete key should be used.
 */
int storage_transaction_commit(uint64_t, const uint8_t[32],
    uint8_t whichkey);

/**
 * storage_directory_read(machinenum, class, key, flist, nfiles):
 * Fetch a sorted list of files in the specified class.  If ${key} is 0, use
 * NETPACKET_DIRECTORY requests (using the read key); otherwise, use
 * NETPACKET_DIRECTORY_D requests (using the delete key).  Return the list
 * and the number of files via ${flist} and ${nfiles} respectively.
 */
int storage_directory_read(uint64_t, char, int, uint8_t **, size_t *);

#endif /* !_STORAGE_H_ */
