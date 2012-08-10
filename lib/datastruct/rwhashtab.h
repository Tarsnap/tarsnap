#ifndef _RWHASHTAB_H_
#define _RWHASHTAB_H_

#include <stddef.h>	/* size_t */
#include <stdint.h>	/* uint8_t */

/**
 * Structure used to store RW hash table state.
 */
typedef struct rwhashtab_internal RWHASHTAB;

/**
 * rwhashtab_init(keyoffset, keylength):
 * Create an empty hash table for storing records which contain keys of
 * length keylength bytes starting at offset keyoffset from the start
 * of each record.  Returns NULL on failure.
 */
RWHASHTAB * rwhashtab_init(size_t, size_t);

/**
 * rwhashtab_getsize(table):
 * Return the number of entries in the table.
 */
size_t rwhashtab_getsize(RWHASHTAB *);

/**
 * rwhashtab_insert(table, record):
 * Insert the provided record into the hash table.  Returns (-1) on error,
 * 0 on success, and 1 if the table already contains a record with the same
 * key.
 */
int rwhashtab_insert(RWHASHTAB *, void *);

/**
 * rwhashtab_read(table, key):
 * Return a pointer to the record in the table with the specified key, or
 * NULL if no such record exists.
 */
void * rwhashtab_read(RWHASHTAB *, const uint8_t *);

/**
 * rwhashtab_foreach(table, func, cookie):
 * Call func(record, cookie) for each record in the hash table.  Stop the
 * iteration early if func returns a non-zero value; return 0 or the
 * non-zero value returned by func.
 */
int rwhashtab_foreach(RWHASHTAB *, int(void *, void *), void *);

/**
 * rwhashtab_free(table):
 * Free the hash table.
 */
void rwhashtab_free(RWHASHTAB *);

#endif /* !_RWHASHTAB_H_ */
