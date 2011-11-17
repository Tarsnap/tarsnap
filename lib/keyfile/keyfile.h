#ifndef _KEYFILE_H_
#define _KEYFILE_H_

#include <stdint.h>
#include <stdio.h>

/**
 * keyfile_read(filename, machinenum, keys):
 * Read keys from a tarsnap key file; and return the machine # via the
 * provided pointer.  Ignore any keys not specified in the ${keys} mask.
 */
int keyfile_read(const char *, uint64_t *, int);

/**
 * keyfile_write(filename, machinenum, keys, passphrase, maxmem, cputime):
 * Write a key file for the specified machine containing the specified keys.
 * If passphrase is non-NULL, use up to cputime seconds and maxmem bytes of
 * memory to encrypt the key file.
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
 * If passphrase is non-NULL, use up to cputime seconds and maxmem bytes of
 * memory to encrypt the key file.
 */
int keyfile_write_file(FILE *, uint64_t, int, char *, size_t, double);

#endif /* !_KEYFILE_H_ */
