#ifndef _MPOOL_H_
#define _MPOOL_H_

#include <stdlib.h>

/**
 * Memory allocator cache.  Memory allocations can be returned to the pool
 * and reused by a subsequent allocation without returning all the way to
 * free/malloc.  In effect, this is an optimization for the case where we
 * know we will want another allocation of the same size soon, at the expense
 * of allowing the memory to be reused by some other code.
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
	void * top;						\
	int atexit_set;						\
} mpool_##name##_rec = {0, NULL, 0};				\
								\
static void							\
mpool_##name##_atexit(void)					\
{								\
	void * top;						\
								\
	while ((top = mpool_##name##_rec.top) != NULL) {	\
		mpool_##name##_rec.top = *(void **)top;		\
		free(top);					\
	}							\
}								\
								\
static inline type *						\
mpool_##name##_malloc(void)					\
{								\
	type * p;						\
								\
	if (mpool_##name##_rec.stacklen) {			\
		p = mpool_##name##_rec.top;			\
		mpool_##name##_rec.top = *(void **)p;		\
		mpool_##name##_rec.stacklen -= 1;		\
	} else {						\
		if (mpool_##name##_rec.atexit_set == 0) {	\
			atexit(mpool_##name##_atexit);		\
			mpool_##name##_rec.atexit_set = 1;	\
		}						\
		p = malloc((sizeof(type) > sizeof(void *)) ?	\
		    sizeof(type) : sizeof(void *));		\
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
		*(void **)p = mpool_##name##_rec.top;		\
		mpool_##name##_rec.top = p;			\
		mpool_##name##_rec.stacklen += 1;		\
	} else {						\
		free(p);					\
	}							\
}								\
								\
struct mpool_##name##_dummy

#endif /* !_MPOOL_H_ */
