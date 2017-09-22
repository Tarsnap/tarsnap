#ifndef _MPOOL_H_
#define _MPOOL_H_

#include <stdlib.h>

/**
 * Memory allocator cache.  Memory allocations can be returned to the pool
 * and reused by a subsequent allocation without returning all the way to
 * free/malloc.  In effect, this is an optimization for the case where we
 * know we will want another allocation of the same size soon, at the expense
 * of keeping memory allocated (and thus preventing any other code from
 * allocating the same memory).
 */

/**
 * MPOOL(name, type, size):
 * Define the functions
 *
 * ${type} * mpool_${name}_malloc(void);
 * void mpool_${name}_free(${type} *);
 *
 * which allocate and free structures of type ${type}.  Up to ${size}
 * such structures are kept cached after _free is called in order to
 * allow future _malloc calls to be rapidly serviced.
 *
 * Cached structures will be freed at program exit time in order to aid
 * in the detection of memory leaks.
 */
#define MPOOL(name, type, size) 				\
static struct mpool_##name##_struct {				\
	size_t stacklen;					\
	int atexit_set;						\
	type * allocs[size];					\
} mpool_##name##_rec = {0, 0, {NULL}};				\
								\
static void							\
mpool_##name##_atexit(void)					\
{								\
								\
	while (mpool_##name##_rec.stacklen) {			\
		--mpool_##name##_rec.stacklen;			\
		free(mpool_##name##_rec.allocs[mpool_##name##_rec.stacklen]);	\
	}							\
}								\
								\
static inline type *						\
mpool_##name##_malloc(void)					\
{								\
	type * p;						\
								\
	if (mpool_##name##_rec.stacklen) {			\
		mpool_##name##_rec.stacklen -= 1;		\
		p = mpool_##name##_rec.allocs[mpool_##name##_rec.stacklen];	\
	} else {						\
		if (mpool_##name##_rec.atexit_set == 0) {	\
			atexit(mpool_##name##_atexit);		\
			mpool_##name##_rec.atexit_set = 1;	\
		}						\
		p = malloc(sizeof(type));			\
	}							\
								\
	return (p);						\
}								\
								\
static inline void						\
mpool_##name##_free(type * p)					\
{								\
								\
	if (p == NULL)						\
		return;						\
								\
	if (mpool_##name##_rec.stacklen < size) {		\
		mpool_##name##_rec.allocs[mpool_##name##_rec.stacklen] = p;	\
		mpool_##name##_rec.stacklen++;			\
	} else {						\
		free(p);					\
	}							\
}								\
								\
struct mpool_##name##_dummy

#endif /* !_MPOOL_H_ */
