#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "elasticarray.h"

struct elasticarray {
	size_t size;
	size_t alloc;
	void * buf;
};

/**
 * resize(EA, nsize):
 * Resize the virtual buffer for ${EA} to length ${nsize} bytes.  The actual
 * buffer may or may not need to be resized.  On failure, the buffer will be
 * unmodified.
 */
static int
resize(struct elasticarray * EA, size_t nsize)
{
	size_t nalloc;
	void * nbuf;

	/* Figure out how large an allocation we want. */
	if (EA->alloc < nsize) {
		/* We need to enlarge the buffer. */
		nalloc = EA->alloc * 2;

		/*
		 * Handle if nalloc is not large enough to hold nsize, which can
		 * happen when: 1) EA->alloc is small enough that EA->alloc * 2
		 * is still smaller than nsize, or 2) EA->alloc is large enough
		 * that EA->alloc * 2 wraps around, becoming smaller than nsize.
		 */
		if (nalloc < nsize)
			nalloc = nsize;
	} else if (EA->alloc / 4 > nsize) {
		/* We need to shrink the buffer. */
		nalloc = nsize * 2;
	} else {
		nalloc = EA->alloc;
	}

	/*
	 * Resizing to zero needs special handling due to some strange
	 * interpretations of what realloc(p, 0) should do if it fails
	 * to allocate zero bytes of memory.
	 */
	if (nalloc == 0) {
		/* We can only get here if we set nsize to 0. */
		assert(nsize == 0);

		free(EA->buf);
		EA->buf = NULL;
		EA->alloc = 0;
	} else if (nalloc != EA->alloc) {
		/* Reallocate to a nonzero size if necessary. */
		if ((nbuf = realloc(EA->buf, nalloc)) == NULL)
			goto err0;
		EA->buf = nbuf;
		EA->alloc = nalloc;
	}

	/* Record the new array size. */
	EA->size = nsize;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * elasticarray_init(nrec, reclen):
 * Create and return an elastic array holding ${nrec} (uninitialized) records
 * of length ${reclen}.  Takes O(nrec * reclen) time.  The value ${reclen}
 * must be positive.
 */
struct elasticarray *
elasticarray_init(size_t nrec, size_t reclen)
{
	struct elasticarray * EA;

	/* Sanity check. */
	assert(reclen > 0);

	/* Allocate structure. */
	if ((EA = malloc(sizeof(struct elasticarray))) == NULL)
		goto err0;

	/* The array is empty for now. */
	EA->size = EA->alloc = 0;
	EA->buf = NULL;

	/* Reallocate to the requested length. */
	if (elasticarray_resize(EA, nrec, reclen))
		goto err1;

	/* Success! */
	return (EA);

err1:
	elasticarray_free(EA);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * elasticarray_resize(EA, nrec, reclen):
 * Resize the elastic array pointed to by ${EA} to hold ${nrec} records of
 * length ${reclen}.  If ${nrec} exceeds the number of records previously
 * held by the array, the additional records will be uninitialized.  Takes
 * O(nrec * reclen) time.  The value ${reclen} must be positive.
 */
int
elasticarray_resize(struct elasticarray * EA, size_t nrec, size_t reclen)
{

	/* Check for overflow. */
	if (nrec > SIZE_MAX / reclen) {
		errno = ENOMEM;
		goto err0;
	}

	/* Resize the buffer. */
	if (resize(EA, nrec * reclen))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * elasticarray_getsize(EA, reclen):
 * Return the number of length-${reclen} records in the array, rounding down
 * if there is a partial record (which can only occur if elasticarray_*
 * functions have been called with different values of reclen).  The value
 * ${reclen} must be positive.
 */
size_t
elasticarray_getsize(const struct elasticarray * EA, size_t reclen)
{

	return (EA->size / reclen);
}

/**
 * elasticarray_append(EA, buf, nrec, reclen):
 * Append to the elastic array ${EA} the ${nrec} records of length ${reclen}
 * stored in ${buf}.  Takes O(nrec * reclen) amortized time.  The value
 * ${reclen} must be positive.
 */
int
elasticarray_append(struct elasticarray * EA,
    const void * buf, size_t nrec, size_t reclen)
{
	size_t bufpos = EA->size;
	size_t nsize;

	/* Check for overflow. */
	if ((nrec > SIZE_MAX / reclen) ||
	    (nrec * reclen > SIZE_MAX - EA->size)) {
		errno = ENOMEM;
		goto err0;
	}

	/* Resize the buffer. */
	nsize = EA->size + nrec * reclen;
	if (resize(EA, nsize))
		goto err0;

	/* Copy bytes in. */
	if (nrec > 0) {
		/* We shouldn't have requested a 0-size array. */
		assert(nsize > 0);

		/* Copy bytes in. */
		memcpy((uint8_t *)(EA->buf) + bufpos, buf, nrec * reclen);
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * elasticarray_shrink(EA, nrec, reclen):
 * Delete the final ${nrec} records of length ${reclen} from the elastic
 * array ${EA}.  If there are fewer than ${nrec} records, all records
 * present will be deleted.  The value ${reclen} must be positive.
 *
 * As an exception to the normal rule, an elastic array may occupy more than
 * 4 times the optimal storage immediately following an elasticarray_shrink()
 * call; but only if realloc(3) failed to shrink a memory allocation.
 */
void
elasticarray_shrink(struct elasticarray * EA, size_t nrec, size_t reclen)
{
	size_t nsize;

	/* Figure out how much to keep. */
	if ((nrec > SIZE_MAX / reclen) ||
	    (nrec * reclen > EA->size))
		nsize = 0;
	else
		nsize = EA->size - nrec * reclen;

	/* Resize the buffer... */
	if (resize(EA, nsize)) {
		/*
		 * ... and if we fail to reallocate, just record the new
		 * length and continue using the old buffer.
		 */
		EA->size = nsize;
	}
}

/**
 * elasticarray_truncate(EA):
 * Release any spare space in the elastic array ${EA}.
 */
int
elasticarray_truncate(struct elasticarray * EA)
{
	void * nbuf;

	/*
	 * Truncating down to zero needs special handling due to some strange
	 * interpretations of what realloc(p, 0) should do if it fails to
	 * allocate zero bytes of memory.
	 */
	if (EA->size == 0) {
		free(EA->buf);
		EA->buf = NULL;
		EA->alloc = 0;
	} else if (EA->alloc > EA->size) {
		/* Reallocate to eliminate spare space if necessary. */
		if ((nbuf = realloc(EA->buf, EA->size)) == NULL)
			goto err0;
		EA->buf = nbuf;
		EA->alloc = EA->size;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * elasticarray_get(EA, pos, reclen):
 * Return a pointer to record number ${pos} of length ${reclen} in the
 * elastic array ${EA}.  Takes O(1) time.
 */
void *
elasticarray_get(struct elasticarray * EA, size_t pos, size_t reclen)
{

	/* Calculate the pointer address. */
	return ((uint8_t *)(EA->buf) + pos * reclen);
}

/**
 * elasticarray_free(EA):
 * Free the elastic array ${EA}.  Takes O(1) time.
 */
void
elasticarray_free(struct elasticarray * EA)
{

	/* Behave consistently with free(NULL). */
	if (EA == NULL)
		return;

	free(EA->buf);
	free(EA);
}

/**
 * elasticarray_export(EA, buf, nrec, reclen):
 * Return the data in the elastic array ${EA} as a buffer ${buf} containing
 * ${nrec} records of length ${reclen}.  Free the elastic array ${EA}.
 * The value ${reclen} must be positive.
 */
int
elasticarray_export(struct elasticarray * EA, void ** buf, size_t * nrec,
    size_t reclen)
{

	/* Remove any spare space. */
	if (elasticarray_truncate(EA))
		goto err0;

	/* Return the buffer and number of records. */
	*buf = EA->buf;
	*nrec = elasticarray_getsize(EA, reclen);

	/*
	 * Free the elastic array structure -- but not its buffer, since we've
	 * passed the buffer out to the caller.
	 */
	free(EA);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * elasticarray_exportdup(EA, buf, nrec, reclen):
 * Duplicate the data in the elastic array ${EA} into a buffer ${buf}
 * containing ${nrec} records of length ${reclen}.  (Same as _export, except
 * that the elastic array remains intact.)  The value ${reclen} must be
 * positive.
 */
int
elasticarray_exportdup(const struct elasticarray * EA, void ** buf,
    size_t * nrec, size_t reclen)
{

	/* Allocate buffer for the caller's copy of the data. */
	if ((*buf = malloc(EA->size)) == NULL)
		goto err0;

	/* Copy the data in. */
	memcpy(*buf, EA->buf, EA->size);

	/* Tell the caller how many records we have. */
	*nrec = elasticarray_getsize(EA, reclen);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
