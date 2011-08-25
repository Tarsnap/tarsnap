#ifndef _PATRICIA_H_
#define _PATRICIA_H_

#include <stddef.h>	/* size_t */
#include <stdint.h>	/* uint8_t */

/**
 * Structure used to store Patricia tree.
 */
typedef struct patricia_internal PATRICIA;

/**
 * patricia_init(void):
 * Create a Patricia tree to be used for mapping arbitrary-length keys to
 * records.  Return NULL on failure.
 */
PATRICIA * patricia_init(void);

/**
 * patricia_insert(tree, key, keylen, rec):
 * Associate the provided key of length keylen bytes with the pointer rec,
 * which must be non-NULL.  Return (-1) on error, 0 on success, and 1 if the
 * key already exists.
 */
int patricia_insert(PATRICIA *, const uint8_t *, size_t, void *);

/**
 * patricia_lookup(tree, key, keylen):
 * Look up the provided key of length keylen bytes.  Return a pointer to the
 * associated _record pointer_ if the key is present in the tree (this can
 * be used to change the record pointer associated with the key); or NULL
 * otherwise.
 *
 * Note that a record can be deleted from a Patricia tree as follows:
 * void ** recp = patricia_lookup(tree, key, keylen);
 * if (recp != NULL)
 *     *recp = NULL;
 * but this does not reduce the memory used by the tree as one might expect
 * from reducing its size.
 */
void ** patricia_lookup(PATRICIA *, const uint8_t *, size_t);

/**
 * patricia_foreach(tree, func, cookie):
 * Traverse the tree in lexicographical order of stored keys, and call
 * func(cookie, key, keylen, rec) for each (key, record) pair.  Stop the
 * traversal early if func returns a non-zero value; return zero, the
 * non-zero value returned by func, or (-1) if an error occurs in the
 * tree traversal.
 */
int patricia_foreach(PATRICIA *, int(void *, uint8_t *, size_t, void *),
    void *);

/**
 * patricia_free(tree):
 * Free the tree.
 */
void patricia_free(PATRICIA *);

#endif /* !_PATRICIA_H_ */
