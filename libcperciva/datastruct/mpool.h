#ifndef MPOOL_H_
#define MPOOL_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ctassert.h"

/**
 * Memory allocator cache.  Memory allocations can be returned to the pool
 * and reused by a subsequent allocation without returning all the way to
 * free/malloc.  In effect, this is an optimization for the case where we
 * know we will want another allocation of the same size soon, at the expense
 * of keeping memory allocated (and thus preventing any other code from
 * allocating the same memory).
 */

/* Internal data. */
struct mpool {
	size_t stacklen;
	size_t allocsize;
	void ** allocs;
	uint64_t nallocs;
	uint64_t nempties;
	int state;
	void ** allocs_static;
	void (* atexitfunc)(void);
};

static inline void
mpool_atexit(struct mpool * M)
{

	/* Free all items on the stack. */
	while (M->stacklen)
		free(M->allocs[--M->stacklen]);

	/* If we allocated a stack, free it. */
	if (M->allocs != M->allocs_static)
		free(M->allocs);
}

static inline void *
mpool_malloc(struct mpool * M, size_t len)
{

	/* Count the total number of allocation requests. */
	M->nallocs++;

	/* If we have an object on the stack, use that. */
	if (M->stacklen)
		return (M->allocs[--(M->stacklen)]);

	/* Count allocation requests where the pool was already empty. */
	M->nempties++;

	/* Initialize the atexit function (the first time we reach here). */
	if (M->state == 0) {
		atexit(M->atexitfunc);
		M->state = 1;
	}

	/* Allocate a new object. */
	return (malloc(len));
}

static inline void
mpool_free(struct mpool * M, void * p)
{
	void ** allocs_new;

	/* Behave consistently with free(NULL). */
	if (p == NULL)
		return;

	/* If we have space in the stack, cache the object. */
	if (M->stacklen < M->allocsize) {
		M->allocs[M->stacklen++] = p;
		return;
	}

	/*
	 * Autotuning: If more than 1/256 of mpool_malloc() calls resulted in
	 * a malloc(), double the stack.
	 */
	if (M->nempties > (M->nallocs >> 8)) {
		/* Sanity check. */
		assert(M->allocsize > 0);

		/* Allocate new stack and copy pointers into it. */
		allocs_new = (void **)malloc(M->allocsize * 2 * sizeof(void *));
		if (allocs_new) {
			memcpy(allocs_new, M->allocs,
			    M->allocsize * sizeof(void *));
			if (M->allocs != M->allocs_static)
				free(M->allocs);
			M->allocs = allocs_new;
			M->allocsize = M->allocsize * 2;
			M->allocs[M->stacklen++] = p;
		} else
			free(p);
	} else
		free(p);

	/* Reset statistics. */
	M->nempties = 0;
	M->nallocs = 0;
}

/**
 * MPOOL(name, type, size):
 * Define the functions
 *
 * ${type} * mpool_${name}_malloc(void);
 * void mpool_${name}_free(${type} *);
 *
 * which allocate and free structures of type ${type}.  A minimum of ${size}
 * such structures are kept cached after _free is called in order to allow
 * future _malloc calls to be rapidly serviced; this limit will be autotuned
 * upwards depending on the allocation/free pattern.
 *
 * Cached structures will be freed at program exit time in order to aid
 * in the detection of memory leaks.
 */
#define MPOOL(name, type, size)					\
static void mpool_##name##_atexit(void);			\
static void * mpool_##name##_static[size];			\
static struct mpool mpool_##name##_rec =			\
    {0, size, mpool_##name##_static, 0, 0, 0,			\
    mpool_##name##_static, mpool_##name##_atexit};		\
								\
CTASSERT(size > 0);						\
								\
static void							\
mpool_##name##_atexit(void)					\
{								\
								\
	mpool_atexit(&mpool_##name##_rec);			\
}								\
								\
static inline type *						\
mpool_##name##_malloc(void)					\
{								\
								\
	return (mpool_malloc(&mpool_##name##_rec, sizeof(type)));	\
}								\
								\
static inline void						\
mpool_##name##_free(type * p)					\
{								\
								\
	mpool_free(&mpool_##name##_rec, p);			\
}								\
								\
static void (* mpool_##name##_dummyptr)(void);			\
static inline void						\
mpool_##name##_dummyfunc(void)					\
{								\
								\
	(void)mpool_##name##_malloc;				\
	(void)mpool_##name##_free;				\
	(void)mpool_##name##_dummyptr;				\
}								\
static void (* mpool_##name##_dummyptr)(void) = mpool_##name##_dummyfunc; \
struct mpool_##name##_dummy

#endif /* !MPOOL_H_ */
