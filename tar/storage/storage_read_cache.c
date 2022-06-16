#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "rwhashtab.h"

#include "storage_read_cache.h"

struct storage_read_cache {
	RWHASHTAB * ht;
	struct read_file_cached * mru;	/* Most recently used. */
	struct read_file_cached * lru;	/* LRU of !evicted files. */
	size_t sz;
	size_t maxsz;
};

struct read_file_cached {
	uint8_t classname[33];
	uint8_t * buf;				/* NULL if !inqueue. */
	size_t buflen;
	struct read_file_cached * next_lru;	/* Less recently used. */
	struct read_file_cached * next_mru;	/* More recently used. */
	int inqueue;
};

/**
 * storage_read_cache_init(void):
 * Allocate and initialize the cache.
 */
struct storage_read_cache *
storage_read_cache_init(void)
{
	struct storage_read_cache * cache;

	/* Allocate the structure. */
	if ((cache = malloc((sizeof(struct storage_read_cache)))) == NULL)
		goto err0;

	/* No cached data yet. */
	cache->lru = NULL;
	cache->mru = NULL;
	cache->sz = 0;
	cache->maxsz = SIZE_MAX;

	/* Create a hash table for cached blocks. */
	if ((cache->ht = rwhashtab_init(offsetof(struct read_file_cached,
	    classname), 33)) == NULL)
		goto err1;

	/* Success! */
	return (cache);

err1:
	free(cache);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * cache_lru_remove(cache, CF):
 * Remove ${CF} from its current position in the LRU queue for ${cache}.
 */
static void
cache_lru_remove(struct storage_read_cache * cache,
    struct read_file_cached * CF)
{

	/* Sanity check: We should be in the queue. */
	assert(CF != NULL);
	assert(CF->inqueue);

	/* Our LRU file is now someone else's LRU file. */
	if (CF->next_mru != NULL)
		CF->next_mru->next_lru = CF->next_lru;
	else
		cache->mru = CF->next_lru;

	/* Our MRU file is now someone else's MRU file. */
	if (CF->next_lru != NULL)
		CF->next_lru->next_mru = CF->next_mru;
	else
		cache->lru = CF->next_mru;

	/* We're no longer in the queue. */
	CF->inqueue = 0;
	cache->sz -= CF->buflen;

	/* We no longer have an MRU or LRU file. */
	CF->next_mru = NULL;
	CF->next_lru = NULL;
}

/**
 * cache_lru_add(cache, CF):
 * Record ${CF} as the most recently used cached file in ${cache}.
 */
static void
cache_lru_add(struct storage_read_cache * cache, struct read_file_cached * CF)
{

	/* Sanity check: We should not be in the queue yet. */
	assert(CF->inqueue == 0);

	/* Nobody is more recently used than us... */
	CF->next_mru = NULL;

	/* ... the formerly MRU file is less recently used than us... */
	CF->next_lru = cache->mru;

	/* ... we're more recently used than any formerly MRU file... */
	if (CF->next_lru != NULL)
		CF->next_lru->next_mru = CF;

	/* ... and more recently used than nothing... */
	if (cache->lru == NULL)
		cache->lru = CF;

	/* ... and we're now the MRU file. */
	cache->mru = CF;

	/* We're now in the queue. */
	CF->inqueue = 1;
	cache->sz += CF->buflen;
}

/**
 * cache_prune(cache):
 * Prune the cache ${cache} down to size.
 */
static void
cache_prune(struct storage_read_cache * cache)
{
	struct read_file_cached * CF;

	/* While the cache is too big... */
	while (cache->sz > cache->maxsz) {
		/* Find the LRU cached file. */
		CF = cache->lru;

		/* Remove this file from the LRU list. */
		cache_lru_remove(cache, CF);

		/* Free its data. */
		free(CF->buf);
		CF->buf = NULL;
		CF->buflen = 0;
	}
}

/**
 * storage_read_cache_add_name(cache, class, name):
 * Add the file ${name} from class ${class} into the ${cache}.  No data is
 * stored yet.
 */
int
storage_read_cache_add_name(struct storage_read_cache * cache, char class,
    const uint8_t name[32])
{
	uint8_t classname[33];
	struct read_file_cached * CF;

	/* Prune the cache if necessary. */
	cache_prune(cache);

	/* Is this file already marked as needing to be cached? */
	classname[0] = (uint8_t)class;
	memcpy(&classname[1], name, 32);
	if ((CF = rwhashtab_read(cache->ht, classname)) != NULL) {
		/* If we're in the linked list, remove ourselves from it. */
		if (CF->inqueue)
			cache_lru_remove(cache, CF);

		/* Insert ourselves at the head of the list. */
		cache_lru_add(cache, CF);

		/* That's all we need to do. */
		goto done;
	}

	/* Allocate a structure. */
	if ((CF = malloc(sizeof(struct read_file_cached))) == NULL)
		goto err0;
	memcpy(CF->classname, classname, 33);
	CF->buf = NULL;
	CF->buflen = 0;
	CF->inqueue = 0;

	/* Add it to the cache. */
	if (rwhashtab_insert(cache->ht, CF))
		goto err1;

	/* Add it to the LRU queue. */
	cache_lru_add(cache, CF);

done:
	/* Success! */
	return (0);

err1:
	free(CF);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_read_cache_set_limit(cache, size):
 * Set a limit of ${size} bytes on the ${cache}.
 */
void
storage_read_cache_set_limit(struct storage_read_cache * cache, size_t size)
{

	/* Record the new size limit. */
	cache->maxsz = size;
}

/**
 * storage_read_cache_add_data(cache, class, name, buf, buflen):
 * If the file ${name} with class ${class} has previous been flagged for
 * storage in the ${cache} via storage_read_cache_add_name(), add
 * ${buflen} data from ${buf} to the cache.
 */
void
storage_read_cache_add_data(struct storage_read_cache * cache, char class,
    const uint8_t name[32], uint8_t * buf, size_t buflen)
{
	struct read_file_cached * CF;
	uint8_t classname[33];

	classname[0] = (uint8_t)class;
	memcpy(&classname[1], name, 32);

	/* Get the cached file, or bail. */
	if ((CF = rwhashtab_read(cache->ht, classname)) == NULL)
		return;

	/* If the file isn't in the queue, bail. */
	if (CF->inqueue == 0)
		return;

	/* If the file already has some data, bail. */
	if (CF->buf != NULL)
		return;

	/* Allocate space for the data, or bail. */
	if ((CF->buf = malloc(buflen)) == NULL)
		return;

	/* Copy in data and data length. */
	CF->buflen = buflen;
	memcpy(CF->buf, buf, buflen);

	/* We've got more data cached now. */
	cache->sz += CF->buflen;
}

/**
 * storage_read_cache_find(cache, class, name, buf, buflen):
 * Look for a file of class ${class} and name ${name} in the cache.
 * If found, set ${buf} to the stored data, and ${buflen} to its length.
 * If not found, set ${buf} to NULL.
 */
void
storage_read_cache_find(struct storage_read_cache * cache, char class,
    const uint8_t name[32], uint8_t ** buf, size_t * buflen)
{
	uint8_t classname[33];
	struct read_file_cached * CF;

	/* Haven't found it yet. */
	*buf = NULL;
	*buflen = 0;

	/* Search for a cache entry. */
	classname[0] = (uint8_t)class;
	memcpy(&classname[1], name, 32);
	if ((CF = rwhashtab_read(cache->ht, classname)) != NULL) {
		/* Found it! */
		*buf = CF->buf;
		*buflen = CF->buflen;
	}
}

/* Free a cache entry. */
static int
callback_cache_free(void * record, void * cookie)
{
	struct read_file_cached * CF = record;

	(void)cookie; /* UNUSED */

	/* Free the buffer and the structure. */
	free(CF->buf);
	free(CF);

	/* Success! */
	return (0);
}

/**
 * storage_read_cache_free(cache):
 * Free the cache ${cache}.
 */
void
storage_read_cache_free(struct storage_read_cache * cache)
{

	/* Behave consistently with free(NULL). */
	if (cache == NULL)
		return;

	/* Free contents of cache. */
	rwhashtab_foreach(cache->ht, callback_cache_free, NULL);

	/* Free cache. */
	rwhashtab_free(cache->ht);
	free(cache);
}
