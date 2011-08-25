#ifndef _CCACHE_INTERNAL_H_
#define _CCACHE_INTERNAL_H_

#include <sys/stat.h>

#include <stdint.h>

#include "ccache.h"
#include "ctassert.h"
#include "multitape.h"
#include "patricia.h"

/*
 * Maximum number of times tarsnap can be run without accessing a cache
 * entry before the entry is removed from the cache.
 */
#define MAXAGE	10

/* Cache data structure. */
struct ccache_internal {
	PATRICIA *	tree;	/* Tree of ccache_record structures. */
	void *		data;	/* Mmapped data. */
	size_t		datalen;	/* Size of mmapped data. */
	size_t		chunksusage;	/* Memory used by chunks. */
	size_t		trailerusage;	/* Memory used by trailers. */
};

/* An entry stored in the cache. */
struct ccache_record {
	/* Values stored in ccache_record_external structure. */
	ino_t	ino;	/* Inode number. */
	off_t	size;	/* File size. */
	time_t	mtime;	/* Modification time, seconds since epoch. */
	size_t	nch;	/* Number of struct chunkheader records. */
	size_t	tlen;	/* Length of trailer (unchunked data). */
	size_t	tzlen;	/* Length of deflated trailer. */
	int	age;	/* Age of entry in read/write cycles. */

	size_t	nchalloc;	/* Number of records of space allocated. */
	struct chunkheader * chp; /* Points to nch records if non-NULL. */
	uint8_t * ztrailer;	/* Points to deflated trailer if non-NULL. */

	int	flags;	/* CCR_* flags. */
};

#define	CCR_ZTRAILER_MALLOC	1

/* On-disk data structure.  Integers are little-endian. */
struct ccache_record_external {
	uint8_t	ino[8];
	uint8_t	size[8];
	uint8_t	mtime[8];
	uint8_t	nch[8];
	uint8_t tlen[4];
	uint8_t tzlen[4];
	uint8_t	prefixlen[4];
	uint8_t suffixlen[4];
	uint8_t	age[4];
	/* Immediately following each record is suffix[]. */
};
/**
 * After all of the ccache_record_external and suffix[] pairs, the
 * struct chunkheader chp[] and uint8_t ztrailer[] data is stored in the
 * same order.
 */

/* Make sure the compiler isn't padding inappropriately. */
CTASSERT(sizeof(struct ccache_record_external) == 52);

#endif /* !_CCACHE_INTERNAL_H_ */
