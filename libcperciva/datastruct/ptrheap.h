#ifndef _PTRHEAP_H_
#define _PTRHEAP_H_

#include <stddef.h>

/**
 * Pointer-heap data structure.  Arbitrary pointers can be inserted and are
 * compared using a provided callback; the usual heapy getmin / increasemin /
 * deletemin algorithms are supported.  To use three additional functions,
 * ptrheap_{delete, increase, decrease}, a setreccookie callback needs to be
 * provided.  These functions require a record cookie to identify the element
 * to operate upon; each time a record's record cookie changes, the
 * setreccookie callback will be called.  Functions return NULL or (int)(-1)
 * on error and set errno; other return types indicate that failure is not
 * possible.  On error, the heap will be unmodified.
 */

/* Opaque pointer-heap type. */
struct ptrheap;

/**
 * ptrheap_init(compar, setreccookie, cookie):
 * Create and return an empty heap.  The function ${compar}(${cookie}, x, y)
 * should return less than, equal to, or greater than 0 depending on whether
 * x is less than, equal to, or greater than y; and if ${setreccookie} is
 * non-zero it will be called as ${setreccookie}(${cookie}, ${ptr}, ${rc}) to
 * indicate that the value ${rc} is the current record cookie for the pointer
 * ${ptr}.  The function ${setreccookie} may not make any ptrheap_* calls.
 */
struct ptrheap * ptrheap_init(int (*)(void *, const void *, const void *),
    void (*)(void *, void *, size_t), void *);

/**
 * ptrheap_create(compar, setreccookie, cookie, N, ptrs):
 * Create and return a heap, as in ptrheap_init, but with the ${N} pointers
 * in ${ptrs} as heap elements.  This is faster than creating an empty heap
 * and adding the elements individually.
 */
struct ptrheap * ptrheap_create(int (*)(void *, const void *, const void *),
    void (*)(void *, void *, size_t), void *, size_t, void **);

/**
 * ptrheap_add(H, ptr):
 * Add the pointer ${ptr} to the heap ${H}.
 */
int ptrheap_add(struct ptrheap *, void *);

/**
 * ptrheap_getmin(H):
 * Return the minimum pointer in the heap ${H}.  If the heap is empty, NULL
 * is returned.
 */
void * ptrheap_getmin(struct ptrheap *);

/**
 * ptrheap_delete(H, rc):
 * Delete from the heap ${H} the element ptr for which the function call
 * setreccookie(cookie, ptr, ${rc}) was most recently made.
 */
void ptrheap_delete(struct ptrheap *, size_t);

/**
 * ptrheap_deletemin(H):
 * Delete the minimum element in the heap ${H}.  The heap must not be empty.
 */
void ptrheap_deletemin(struct ptrheap *);

/**
 * ptrheap_decrease(H, rc):
 * Adjust the heap ${H} to account for the fact that the element ptr for
 * which the function call setreccookie(cookie, ptr, ${rc}) was most recently
 * made has decreased.
 */
void ptrheap_decrease(struct ptrheap *, size_t);

/**
 * ptrheap_increase(H, rc):
 * Adjust the heap ${H} to account for the fact that the element ptr for
 * which the function call setreccookie(cookie, ptr, ${rc}) was most recently
 * made has increased.
 */
void ptrheap_increase(struct ptrheap *, size_t);

/**
 * ptrheap_increasemin(H):
 * Adjust the heap ${H} to account for the fact that the (formerly) minimum
 * element has increased.
 */
void ptrheap_increasemin(struct ptrheap *);

/**
 * ptrheap_free(H):
 * Free the pointer heap ${H}.
 */
void ptrheap_free(struct ptrheap *);

#endif /* !_PTRHEAP_H_ */
