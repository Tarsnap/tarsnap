#ifndef _STORAGE_READ_CACHE_H_
#define _STORAGE_READ_CACHE_H_

#include <stdint.h>
#include <stddef.h>

/**
 * storage_read_cache_init(void):
 * Allocate and initialize the cache.
 */
struct storage_read_cache * storage_read_cache_init(void);

/**
 * storage_read_cache_add_name(cache, class, name):
 * Add the file ${name} from class ${class} into the ${cache}.  No data is
 * stored yet.
 */
int storage_read_cache_add_name(struct storage_read_cache *, char,
    const uint8_t[32]);

/**
 * storage_read_cache_add_data(cache, class, name, buf, buflen):
 * If the file ${name} with class ${class} has previous been flagged for
 * storage in the ${cache} via storage_read_cache_add_name(), add
 * ${buflen} data from ${buf} to the cache.
 */
void storage_read_cache_add_data(struct storage_read_cache *, char,
    const uint8_t[32], uint8_t *, size_t);

/**
 * storage_read_cache_set_limit(cache, size):
 * Set a limit of ${size} bytes on the ${cache}.
 */
void storage_read_cache_set_limit(struct storage_read_cache *, size_t);

/**
 * storage_read_cache_find(cache, class, name, buf, buflen):
 * Look for a file of class ${class} and name ${name} in the cache.
 * If found, set ${buf} to the stored data, and ${buflen} to its length.
 * If not found, set ${buf} to NULL.
 */
void storage_read_cache_find(struct storage_read_cache *, char,
    const uint8_t[32], uint8_t **, size_t *);

/**
 * storage_read_cache_free(cache):
 * Free the cache ${cache}.
 */
void storage_read_cache_free(struct storage_read_cache *);

#endif /* !_STORAGE_READ_CACHE_H_ */
