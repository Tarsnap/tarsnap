#ifndef _ELASTICARRAY_H_
#define _ELASTICARRAY_H_

#include <stddef.h>

/**
 * Elastic Arrays are dynamically resizing arrays which remain within a
 * factor of 4 of the optimal size for the data they contain and have (within
 * a constant factor) amortized optimal running time providing that all of
 * the allocated space is accessed at some point.  Functions return NULL or
 * (int)(-1) on error and set errno; other return types indicate that failure
 * is not possible.  On error, the array will be unmodified.
 *
 * The ELASTICARRAY_DECL(type, prefix, rectype) macro can be used to create a
 * more friendly interface, at the expense of restricting the array to only
 * holding a single data type.
 */

/* Opaque elastic array type. */
struct elasticarray;

/**
 * elasticarray_init(nrec, reclen):
 * Create and return an elastic array holding ${nrec} (uninitialized) records
 * of length ${reclen}.  Takes O(nrec * reclen) time.
 */
struct elasticarray * elasticarray_init(size_t, size_t);

/**
 * elasticarray_resize(EA, nrec, reclen):
 * Resize the elastic array pointed to by ${EA} to hold ${nrec} records of
 * length ${reclen}.  If ${nrec} exceeds the number of records previously
 * held by the array, the additional records will be uninitialized.  Takes
 * O(nrec * reclen) time.
 */
int elasticarray_resize(struct elasticarray *, size_t, size_t);

/**
 * elasticarray_getsize(EA, reclen):
 * Return the number of length-${reclen} records in the array, rounding down
 * if there is a partial record (which can only occur if elasticarray_*
 * functions have been called with different values of reclen).  The value
 * ${reclen} must be positive.
 */
size_t elasticarray_getsize(struct elasticarray *, size_t);

/**
 * elasticarray_append(EA, buf, nrec, reclen):
 * Append to the elastic array ${EA} the ${nrec} records of length ${reclen}
 * stored in ${buf}.  Takes O(nrec * reclen) amortized time.
 */
int elasticarray_append(struct elasticarray *, const void *, size_t, size_t);

/**
 * elasticarray_shrink(EA, nrec, reclen):
 * Delete the final ${nrec} records of length ${reclen} from the elastic
 * array ${EA}.  If there are fewer than ${nrec} records, all records
 * present will be deleted.
 *
 * As an exception to the normal rule, an elastic array may occupy more than
 * 4 times the optimal storage immediately following an elasticarray_shrink
 * call; but only if realloc(3) failed to shrink a memory allocation.
 */
void elasticarray_shrink(struct elasticarray *, size_t, size_t);

/**
 * elasticarray_truncate(EA):
 * Release any spare space in the elastic array ${EA}.
 */
int elasticarray_truncate(struct elasticarray *);

/**
 * elasticarray_get(EA, pos, reclen):
 * Return a pointer to record number ${pos} of length ${reclen} in the
 * elastic array ${EA}.  Takes O(1) time.
 */
void * elasticarray_get(struct elasticarray *, size_t, size_t);

/**
 * elasticarray_free(EA):
 * Free the elastic array ${EA}.  Takes O(1) time.
 */
void elasticarray_free(struct elasticarray *);

/**
 * ELASTICARRAY_DECL(type, prefix, rectype):
 * Declare the type ${type} and the following functions:
 * ${type} ${prefix}_init(size_t nrec);
 * int ${prefix}_resize(${type} EA, size_t nrec);
 * size_t ${prefix}_getsize(${type} EA);
 * int ${prefix}_append(${type} EA, const void * buf, size_t nrec);
 * void ${prefix}_shrink(${type} EA, size_t nrec);
 * int ${prefix}_truncate(${type} EA);
 * ${rectype} * ${prefix}_get(${type} EA, size_t pos);
 * void ${prefix}_free(${type} EA);
 */
#define ELASTICARRAY_DECL(type, prefix, rectype)			\
	static inline struct prefix##_struct *				\
	prefix##_init(size_t nrec)					\
	{								\
		struct elasticarray * EA;				\
									\
		EA = elasticarray_init(nrec, sizeof(rectype));		\
		return ((struct  prefix##_struct *)EA);			\
	}								\
	static inline int						\
	prefix##_resize(struct prefix##_struct * EA, size_t nrec)	\
	{								\
		return (elasticarray_resize((struct elasticarray *)EA,	\
		    nrec, sizeof(rectype)));				\
	}								\
	static inline size_t						\
	prefix##_getsize(struct prefix##_struct * EA)			\
	{								\
		return (elasticarray_getsize((struct elasticarray *)EA,	\
		    sizeof(rectype)));					\
	}								\
	static inline int						\
	prefix##_append(struct prefix##_struct * EA,			\
	    const void * buf, size_t nrec)				\
	{								\
		return (elasticarray_append((struct elasticarray *)EA,	\
		    buf, nrec, sizeof(rectype)));			\
	}								\
	static inline void						\
	prefix##_shrink(struct prefix##_struct * EA, size_t nrec)	\
	{								\
		elasticarray_shrink((struct elasticarray *)EA,		\
		    nrec, sizeof(rectype));				\
	}								\
	static inline int						\
	prefix##_truncate(struct prefix##_struct * EA)			\
	{								\
		return (elasticarray_truncate(				\
		    (struct elasticarray *)EA));			\
	}								\
	static inline rectype *						\
	prefix##_get(struct prefix##_struct * EA, size_t pos)		\
	{								\
		void * rec;						\
									\
		rec = elasticarray_get((struct elasticarray *)EA,	\
		    pos, sizeof(rectype));				\
		return ((rectype *)rec);				\
	}								\
	static inline void						\
	prefix##_free(struct prefix##_struct * EA)			\
	{								\
		elasticarray_free((struct elasticarray *)EA);		\
	}								\
	typedef struct prefix##_struct * type

#endif /* !_ELASTICARRAY_H_ */
