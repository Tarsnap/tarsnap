#ifndef KEYFILE_H_
#define KEYFILE_H_

#include <stdint.h>
#include <stdio.h>

#include "passphrase_entry.h"

/**
 * keyfile_read(filename, machinenum, keys, force, passphrase_entry,
 *     passphrase_arg):
 * Read keys from a tarsnap key file; and return the machine # via the
 * provided pointer.  Ignore any keys not specified in the ${keys} mask.
 * If ${force} is 1, do not check whether decryption will exceed
 * the estimated available memory or time.  Use the ${passphrase_entry}
 * method to read the passphrase, using ${passphrase_arg} if applicable.
 */
int keyfile_read(const char *, uint64_t *, int, int, enum passphrase_entry,
    const char *);

/**
 * keyfile_write(filename, machinenum, keys, passphrase, maxmem, cputime):
 * Write a key file for the specified machine containing the specified keys.
 * If ${passphrase} is non-NULL, use up to ${cputime} seconds and ${maxmem}
 * bytes of memory to encrypt the key file.
 */
int keyfile_write(const char *, uint64_t, int, char *, size_t, double);

/**
 * keyfile_write_open(filename):
 * Open a key file for writing.  Avoid race conditions.  Return a FILE *.
 */
FILE * keyfile_write_open(const char *);

/**
 * keyfile_write_file(f, machinenum, keys, passphrase, maxmem, cputime):
 * Write a key file for the specified machine containing the specified keys.
 * If ${passphrase} is non-NULL, use up to ${cputime} seconds and ${maxmem}
 * bytes of memory to encrypt the key file.
 */
int keyfile_write_file(FILE *, uint64_t, int, char *, size_t, double);

#endif /* !KEYFILE_H_ */
