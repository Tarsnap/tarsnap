#ifndef _CCACHE_H_
#define _CCACHE_H_

#include <sys/stat.h>

#include "multitape.h"

typedef struct ccache_internal CCACHE;
typedef struct ccache_entry CCACHE_ENTRY;

/**
 * ccache_read(path):
 * Read the chunkification cache (if present) from the directory ${path};
 * return a patricia tree mapping absolute paths to cache entries.
 */
CCACHE * ccache_read(const char *);

/**
 * ccache_entry_lookup(cache, path, sb, cookie, fullentry):
 * An archive entry is being written for the file ${path} with lstat data
 * ${sb}, to the multitape with write cookie ${cookie}.  Look up the file in
 * the chunkification cache ${cache}, and set ${fullentry} to a non-zero
 * value iff the cache can provide at least sb->st_size bytes of the archive
 * entry.  Return a cookie which can be passed to either ccache_entry_write
 * or ccache_entry_start depending upon whether ${fullentry} is zero or not.
 */
CCACHE_ENTRY * ccache_entry_lookup(CCACHE *, const char *,
    const struct stat *, TAPE_W *, int *);

/**
 * ccache_entry_check_file(cce, fd):
 * Check the chunk hashes and trailer in ${cce} by reading and hashing data
 * from ${fd}.  Assumes that the file that ${fd} points to is fully available
 * in the cache.  Return 0 if file matches, 1 if it does not match, or -1 if
 * an error occurred.
 */
int ccache_entry_check_file(CCACHE_ENTRY *, int);

/**
 * ccache_entry_write(cce, cookie):
 * Write the cached archive entry ${cce} to the multitape with write cookie
 * ${cookie}.  Note that this may only be called if ${cce} was returned by
 * a ccache_entry_lookup which set ${fullentry} to a non-zero value.  Return
 * the length written.
 */
off_t ccache_entry_write(CCACHE_ENTRY *, TAPE_W *);

/**
 * ccache_entry_writefile(cce, cookie, notrailer, fd):
 * Write data from the file descriptor ${fd} to the multitape with write
 * cookie ${cookie}, using the cache entry ${cce} as a hint about how data
 * is chunkified; and set up callbacks from the multitape layer so that the
 * cache entry will be updated with any further chunks and (if ${notrailer}
 * is zero) any trailer.  Return the length written.
 */
off_t ccache_entry_writefile(CCACHE_ENTRY *, TAPE_W *, int, int);

/**
 * ccache_entry_end(cache, cce, cookie, path, snaptime):
 * The archive entry is ending; clean up callbacks, insert the cache entry
 * into the cache if it isn't already present, and free memory.
 */
int ccache_entry_end(CCACHE *, CCACHE_ENTRY *, TAPE_W *, const char *, time_t);

/**
 * ccache_entry_free(cce, cookie):
 * Free the cache entry and cancel callbacks from the multitape layer.
 */
void ccache_entry_free(CCACHE_ENTRY *, TAPE_W *);

/**
 * ccache_write(cache, path):
 * Write the given chunkification cache into the directory ${path}.
 */
int ccache_write(CCACHE *, const char *);

/**
 * ccache_free(cache):
 * Free the cache and all of its entries.
 */
void ccache_free(CCACHE *);

/**
 * ccache_remove(path):
 * Delete the chunkification cache from the directory ${path}.
 */
int ccache_remove(const char *);

#endif /* !_CCACHE_H_ */
