#include "bsdtar_platform.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_entropy.h"
#include "ctassert.h"
#include "sysendian.h"
#include "warnp.h"

#include "crypto.h"

#include "rwhashtab.h"

/*
 * We use le64dec to generate a size_t from an array of bytes; make sure
 * that this is enough.
 */
CTASSERT(sizeof(size_t) <= sizeof(uint64_t));

/**
 * Structure used to store RW hash table state.
 */
struct rwhashtab_internal {
	/* Hash table parameters */
	size_t cursize;		/* Size of table; must be a power of 2 */
	size_t numentries;	/* Number of entries, <= 0.75 * cursize */
	void ** ht;		/* Table of cursize pointers to records */

	/* Where to find keys within records */
	size_t keyoffset;
	size_t keylength;

	/* Used for hashing */
	uint8_t randbuf[32];	/* Prefix used for hashing */
};

static int rwhashtab_enlarge(RWHASHTAB * H);
static size_t rwhashtab_search(RWHASHTAB * H, const uint8_t * key);

/* Enlarge the table by a factor of 2, and rehash. */
static int
rwhashtab_enlarge(RWHASHTAB * H)
{
	void ** htnew, ** htold;
	void * rec;
	size_t newsize, oldsize;
	size_t htposold, htposnew;

	/*
	 * Compute new size, and make sure the new table size in bytes
	 * doesn't overflow.
	 */
	newsize = H->cursize * 2;
	if (newsize > SIZE_MAX / sizeof(void *)) {
		errno = ENOMEM;
		return (-1);
	}

	/* Allocate and zero the new space. */
	if ((htnew = malloc(newsize * sizeof(void *))) == NULL)
		return (-1);
	for (htposnew = 0; htposnew < newsize; htposnew++)
		htnew[htposnew] = NULL;

	/* Attach new space to hash table. */
	htold = H->ht;
	H->ht = htnew;
	oldsize = H->cursize;
	H->cursize = newsize;

	/* Rehash into new table. */
	for (htposold = 0; htposold < oldsize; htposold++) {
		rec = htold[htposold];
		if (rec != NULL) {
			htposnew = rwhashtab_search(H,
			    (uint8_t *)rec + H->keyoffset);
			H->ht[htposnew] = rec;
		}
	}

	/* Free now-unused memory. */
	free(htold);

	/* Success! */
	return (0);
}

/*
 * Search for a record within ht[size], using hashing parameters from H.
 * Return the position of the record or of the first NULL found.
 */
static size_t
rwhashtab_search(RWHASHTAB * H, const uint8_t * key)
{
	size_t htpos;
	uint8_t hashbuf[32];

	/* Compute the hash of the record key. */
	if (crypto_hash_data_2(CRYPTO_KEY_HMAC_SHA256, H->randbuf, 32,
	    key, H->keylength, hashbuf)) {
		warn0("Programmer error: "
		    "SHA256 should never fail");
		abort();
	}

	/* Compute starting hash location. */
	htpos = le64dec(hashbuf) & (H->cursize - 1);

	/*
	 * Search.  This is not an endless loop since the table isn't
	 * allowed to be full.
	 */
	do {
		/* Is the space empty? */
		if (H->ht[htpos] == NULL)
			return (htpos);

		/* Do we have the right key? */
		if (memcmp((uint8_t *)(H->ht[htpos]) + H->keyoffset,
		    key, H->keylength) == 0)
			return (htpos);

		/* Move to the next table entry. */
		htpos = (htpos + 1) & (H->cursize - 1);
	} while (1);
}

/**
 * rwhashtab_init(keyoffset, keylength):
 * Create an empty hash table for storing records which contain keys of
 * length keylength bytes starting at offset keyoffset from the start
 * of each record.  Returns NULL on failure.
 */
RWHASHTAB *
rwhashtab_init(size_t keyoffset, size_t keylength)
{
	RWHASHTAB * H;
	size_t i;

	/* Sanity check. */
	if (keylength == 0)
		return (NULL);

	H = malloc(sizeof(*H));
	if (H == NULL)
		return (NULL);

	H->cursize = 4;
	H->numentries = 0;
	H->keyoffset = keyoffset;
	H->keylength = keylength;

	/* Get some entropy for the keyed hash function. */
	if (crypto_entropy_read(H->randbuf, 32)) {
		free(H);
		return (NULL);
	}

	/* Allocate space for pointers to records. */
	H->ht = malloc(H->cursize * sizeof(void *));
	if (H->ht == NULL) {
		free(H);
		return (NULL);
	}

	/* All of the entries are empty. */
	for (i = 0; i < H->cursize; i++)
		H->ht[i] = NULL;

	return (H);
}

/**
 * rwhashtab_getsize(table):
 * Return the number of entries in the table.
 */
size_t
rwhashtab_getsize(RWHASHTAB * H)
{

	/* Just extract the value and return it. */
	return (H->numentries);
}

/**
 * rwhashtab_insert(table, record):
 * Insert the provided record into the hash table.  Returns (-1) on error,
 * 0 on success, and 1 if the table already contains a record with the same
 * key.
 */
int
rwhashtab_insert(RWHASHTAB * H, void * rec)
{
	int rc;
	size_t htpos;

	/*
	 * Does the table need to be enlarged?  Technically we should check
	 * this after searching to see if the key is already in the table;
	 * but then we'd need to search again to find the new insertion
	 * location after enlarging, which would add unnecessary complexity.
	 * Doing it this way just means that we might enlarge the table one
	 * insert sooner than necessary.
	 */
	if (H->numentries >= H->cursize - (H->cursize >> 2)) {
		rc = rwhashtab_enlarge(H);
		if (rc)
			return (rc);
	}

	/*
	 * Search for the record, to see if it is already present and/or
	 * where it should be inserted.
	 */
	htpos = rwhashtab_search(H, (uint8_t *)rec + H->keyoffset);

	/* Already present? */
	if (H->ht[htpos] != NULL)
		return (1);

	/* Insert the record. */
	H->ht[htpos] = rec;
	H->numentries += 1;
	return (0);
}

/**
 * rwhashtab_read(table, key):
 * Return a pointer to the record in the table with the specified key, or
 * NULL if no such record exists.
 */
void *
rwhashtab_read(RWHASHTAB * H, const uint8_t * key)
{
	size_t htpos;

	/* Search. */
	htpos = rwhashtab_search(H, key);

	/* Return the record, or NULL if not present. */
	return (H->ht[htpos]);
}

/**
 * rwhashtab_foreach(table, func, cookie):
 * Call func(record, cookie) for each record in the hash table.  Stop the
 * iteration early if func returns a non-zero value; return 0 or the
 * non-zero value returned by func.
 */
int
rwhashtab_foreach(RWHASHTAB * H, int func(void *, void *), void * cookie)
{
	size_t htpos;
	int rc;

	for (htpos = 0; htpos < H->cursize; htpos++) {
		if (H->ht[htpos] != NULL) {
			rc = func(H->ht[htpos], cookie);
			if (rc)
				return (rc);
		}
	}

	return (0);
}

/**
 * rwhashtab_free(table):
 * Free the hash table.
 */
void
rwhashtab_free(RWHASHTAB * H)
{

	/* Behave consistently with free(NULL). */
	if (H == NULL)
		return;

	/* Free everything. */
	free(H->ht);
	free(H);
}
