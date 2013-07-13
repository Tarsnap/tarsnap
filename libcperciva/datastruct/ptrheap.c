#include <stdlib.h>

#include "elasticarray.h"

#include "ptrheap.h"

ELASTICARRAY_DECL(PTRLIST, ptrlist, void *);

struct ptrheap {
	int (* compar)(void *, const void *, const void *);
	void (* setreccookie)(void *, void *, size_t);
	void * cookie;
	PTRLIST elems;
	size_t nelems;
};

/**
 * swap(elems, i, j, setreccookie, cookie):
 * Swap elements ${i} and ${j} in ${elems}.  If ${setreccookie} is non-NULL,
 * invoke ${setreccookie}(${cookie}, elem, pos) for each of the elements and
 * their new positions in the tree.
 */
static void
swap(PTRLIST elems, size_t i, size_t j,
    void (* setreccookie)(void *, void *, size_t), void * cookie)
{
	void * tmp;

	/* Swap the elements. */
	tmp = *ptrlist_get(elems, i);
	*ptrlist_get(elems, i) = *ptrlist_get(elems, j);
	*ptrlist_get(elems, j) = tmp;

	/* Notify about the moved elements. */
	if (setreccookie != NULL) {
		setreccookie(cookie, *ptrlist_get(elems, i), i);
		setreccookie(cookie, *ptrlist_get(elems, j), j);
	}
}

/**
 * heapifyup(elems, i, compar, setreccookie, cookie):
 * Sift up element ${i} of the elements ${elems}, using the comparison
 * function ${compar} and the cookie ${cookie}.  If elements move and
 * ${setreccookie} is non-NULL, use it to notify about the updated position
 * of elements in the heap.
 */
static void
heapifyup(PTRLIST elems, size_t i,
    int (* compar)(void *, const void *, const void *),
    void (* setreccookie)(void *, void *, size_t), void * cookie)
{

	/* Iterate up the tree. */
	do {
		/* If we're at the root, we have nothing to do. */
		if (i == 0)
			break;

		/* If this is >= its parent, we're done. */
		if (compar(cookie, *ptrlist_get(elems, i),
		    *ptrlist_get(elems, (i - 1) / 2)) >= 0)
			break;

		/* Swap with the parent. */
		swap(elems, i, (i - 1) / 2, setreccookie, cookie);

		/* Move up the tree. */
		i = (i - 1) / 2;
	} while (1);
}

/**
 * heapify(elems, i, N, compar, setreccookie, cookie):
 * Sift down element number ${i} out of ${N} of the elements ${elems}, using
 * the comparison function ${compar} and the cookie ${cookie}.  If elements
 * move and ${setreccookie} is non-NULL, use it to notify about the updated
 * position of elements in the heap.
 */
static void
heapify(PTRLIST elems, size_t i, size_t N,
    int (* compar)(void *, const void *, const void *),
    void (* setreccookie)(void *, void *, size_t), void * cookie)
{
	size_t min;

	/* Iterate down the tree. */
	do {
		/* Look for the minimum element out of {i, 2i+1, 2i+2}. */
		min = i;

		/* Is this bigger than element 2i+1? */
		if ((2 * i + 1 < N) &&
		    (compar(cookie, *ptrlist_get(elems, min),
			*ptrlist_get(elems, 2 * i + 1)) > 0))
			min = 2 * i + 1;

		/* Is this bigger than element 2i+2? */
		if ((2 * i + 2 < N) &&
		    (compar(cookie, *ptrlist_get(elems, min),
			*ptrlist_get(elems, 2 * i + 2)) > 0))
			min = 2 * i + 2;

		/* If the minimum is i, we have heap-property. */
		if (min == i)
			break;

		/* Move the minimum into position i. */
		swap(elems, min, i, setreccookie, cookie);

		/* Move down the tree. */
		i = min;
	} while (1);
}

/**
 * ptrheap_init(compar, setreccookie, cookie):
 * Create and return an empty heap.  The function ${compar}(${cookie}, x, y)
 * should return less than, equal to, or greater than 0 depending on whether
 * x is less than, equal to, or greater than y; and if ${setreccookie} is
 * non-zero it will be called as ${setreccookie}(${cookie}, ${ptr}, ${rc}) to
 * indicate that the value ${rc} is the current record cookie for the pointer
 * ${ptr}.  The function ${setreccookie} may not make any ptrheap_* calls.
 */
struct ptrheap *
ptrheap_init(int (* compar)(void *, const void *, const void *),
    void (* setreccookie)(void *, void *, size_t), void * cookie)
{

	/* Let ptrheap_create handle this. */
	return (ptrheap_create(compar, setreccookie, cookie, 0, NULL));
}

/**
 * ptrheap_create(compar, setreccookie, cookie, N, ptrs):
 * Create and return a heap, as in ptrheap_init, but with the ${N} pointers
 * in ${ptrs} as heap elements.  This is faster than creating an empty heap
 * and adding the elements individually.
 */
struct ptrheap *
ptrheap_create(int (* compar)(void *, const void *, const void *),
    void (* setreccookie)(void *, void *, size_t), void * cookie,
    size_t N, void ** ptrs)
{
	struct ptrheap * H;
	size_t i;

	/* Allocate structure. */
	if ((H = malloc(sizeof(struct ptrheap))) == NULL)
		goto err0;

	/* Store parameters. */
	H->compar = compar;
	H->setreccookie = setreccookie;
	H->cookie = cookie;

	/* We will have N elements. */
	H->nelems = N;

	/* Allocate space for N heap elements. */
	if ((H->elems = ptrlist_init(N)) == NULL)
		goto err1;

	/* Copy the heap elements in. */
	for (i = 0; i < N; i++)
		*ptrlist_get(H->elems, i) = ptrs[i];

	/* Turn this into a heap. */
	for (i = N - 1; i < N; i--)
		heapify(H->elems, i, N, H->compar, NULL, H->cookie);

	/* Advise the caller about the record cookies. */
	if (H->setreccookie != NULL)
		for (i = 0; i < N; i++)
			(H->setreccookie)(H->cookie,
			    *ptrlist_get(H->elems, i), i);

	/* Success! */
	return (H);

err1:
	free(H);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * ptrheap_add(H, ptr):
 * Add the pointer ${ptr} to the heap ${H}.
 */
int
ptrheap_add(struct ptrheap * H, void * ptr)
{

	/* Add the element to the end of the heap. */
	if (ptrlist_append(H->elems, &ptr, 1))
		goto err0;
	H->nelems += 1;

	/* Advise the caller about the current location of this record. */
	if (H->setreccookie != NULL)
		(H->setreccookie)(H->cookie, ptr, H->nelems - 1);

	/* Move the new element up in the tree if necessary. */
	heapifyup(H->elems, H->nelems - 1,
	    H->compar, H->setreccookie, H->cookie);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * ptrheap_getmin(H):
 * Return the minimum pointer in the heap ${H}.  If the heap is empty, NULL
 * is returned.
 */
void *
ptrheap_getmin(struct ptrheap * H)
{

	/* If we have any elements, the minimum is in position 0. */
	if (H->nelems)
		return (*ptrlist_get(H->elems, 0));
	else
		return (NULL);
}

/**
 * ptrheap_delete(H, rc):
 * Delete from the heap ${H} the element ptr for which the function call
 * setreccookie(cookie, ptr, ${rc}) was most recently made.
 */
void
ptrheap_delete(struct ptrheap * H, size_t rc)
{

	/*
	 * If the element we're deleting is not at the end of the heap,
	 * replace it with the element which is currently at the end.
	 */
	if (rc != H->nelems - 1) {
		/* Move ptr from position H->nelems - 1 into position rc. */
		*ptrlist_get(H->elems, rc) =
		    *ptrlist_get(H->elems, H->nelems - 1);
		if (H->setreccookie != NULL)
			(H->setreccookie)(H->cookie,
			    *ptrlist_get(H->elems, rc), rc);

		/* Is this too small to be in position ${rc}? */
		if ((rc > 0) &&
		    (H->compar(H->cookie, *ptrlist_get(H->elems, rc),
			*ptrlist_get(H->elems, (rc - 1) / 2)) < 0)) {
			/* Swap with the parent, and keep moving up. */
			swap(H->elems, rc, (rc - 1) / 2,
			    H->setreccookie, H->cookie);
			heapifyup(H->elems, (rc - 1) / 2,
			    H->compar, H->setreccookie, H->cookie);
		} else {
			/* Maybe we need to move it down instead? */
			heapify(H->elems, rc, H->nelems,
			    H->compar, H->setreccookie, H->cookie);
		}
	}

	/*
	 * We've got everything we want to keep in positions 0 .. nelems - 2,
	 * and we have heap-nature, so all we need to do is strip off the
	 * final pointer.
	 */
	ptrlist_shrink(H->elems, 1);
	H->nelems--;
}

/**
 * ptrheap_deletemin(H):
 * Delete the minimum element in the heap ${H}.
 */
void
ptrheap_deletemin(struct ptrheap * H)
{

	/* Let ptrheap_delete handle this. */
	ptrheap_delete(H, 0);
}

/**
 * ptrheap_increase(H, rc):
 * Adjust the heap ${H} to account for the fact that the element ptr for
 * which the function call setreccookie(cookie, ptr, ${rc}) was most recently
 * made has incrased.
 */
void
ptrheap_increase(struct ptrheap * H, size_t rc)
{

	/* Move the element down if necessary. */
	heapify(H->elems, rc, H->nelems,
	    H->compar, H->setreccookie, H->cookie);
}

/**
 * ptrheap_increasemin(H):
 * Adjust the heap ${H} to account for the fact that the (formerly) minimum
 * element has increased.
 */
void
ptrheap_increasemin(struct ptrheap * H)
{

	/* Move the element down if necessary. */
	heapify(H->elems, 0, H->nelems,
	    H->compar, H->setreccookie, H->cookie);
}

/**
 * ptrheap_free(H):
 * Free the pointer heap ${H}.
 */
void
ptrheap_free(struct ptrheap * H)
{

	/* Be compatible with free(NULL). */
	if (H == NULL)
		return;

	ptrlist_free(H->elems);
	free(H);
}
