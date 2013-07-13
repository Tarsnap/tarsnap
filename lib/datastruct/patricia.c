#include "bsdtar_platform.h"

#include <stddef.h>	/* size_t */
#include <stdint.h>	/* uint8_t */
#include <stdlib.h>	/* malloc, free */
#include <string.h>	/* memcpy */

#include "patricia.h"

/**
 * Our Patricia tree structure can be thought of as operating on strings of
 * 9-bit bytes, where 0x00 -- 0xFF are mapped to 0x100 -- 0x1FF and 0x00
 * represents the end-of-string character (note that NUL can occur inside
 * keys).  The field (struct pnode).mask is either 0 or a power of 2; if 0,
 * the left child, if non-NULL, is a pointer to the record associated with
 * the key thus far.  For example, the strings "hello", "hello colin",
 * "hello world", and "wednesday", each associated with pointers to
 * themselves, are stored in the following tree:
 *
 *                       [0x10, 0x60, 0, ""]
 *                        |               |
 *   [0x00, 0x00, 5, "hello"]          [0x00, 0x00, 9, "wednesday"]
 *    |                    |              |                 |
 * "hello"     [0x10, 0x60, 1, " "]     "wednesday"        NULL
 *              |                |
 *   [0x00, 0x00, 5, "colin"]   [0x00, 0x00, 5, "world"]
 *    |                    |       |                 |
 * "hello colin"          NULL  "hello world"       NULL
 *
 */

/* Structure used to store a Patricia tree node. */
struct pnode {
	struct pnode *	left;		/* Left child. */
	struct pnode *	right;		/* Right child. */
	uint8_t		mask;		/* Critical bit mask. */
	uint8_t		high;		/* High bits of this node. */
	uint8_t		slen;		/* Length of s[]. */
	uint8_t		s[];		/* Bytes since parent's s[]. */
};

/**
 * The use of a uint8_t to store the number of bytes in (struct pnode).s[]
 * means that we cannot store more than 255 bytes in each node; since many
 * memory allocators can perform allocations of 256 bytes far more
 * efficiently than they can perform allocations of slightly more than 256
 * bytes, we artificially limit the number of bytes stored in each node so
 * that a node is never larger than 256 bytes in total.
 *
 * In some applications it may be preferable to eliminate variable-length
 * memory allocations entirely and provide the same size of memory allocation
 * to each node; if this is done, it would almost certainly be desirable to
 * reduce MAXSLEN further, e.g., to (2 * sizeof(void *) - 3) so that each
 * node would be of size (4 * sizeof(void *)).
 */
/* Maximum number of key bytes stored in a node. */
#ifndef	MAXSLEN
#define	MAXSLEN	(256 - sizeof(struct pnode))
#endif

/*
 * Structure used to store Patricia tree.  The maximum key length is stored
 * in order to simplify buffer handling in the tree traversal code.
 */
struct patricia_internal {
	struct pnode *	root;		/* Root node of tree. */
	size_t		maxkey;		/* Longest key length. */
};

static struct pnode * node_alloc(uint8_t, const uint8_t *);
static struct pnode * node_dup(const struct pnode *, uint8_t, const uint8_t *);
static int compare(const struct pnode *, const uint8_t *, size_t, uint8_t *,
    uint8_t *);
static int foreach_internal(struct pnode *,
    int(void *, uint8_t *, size_t, void *), void *, uint8_t *, size_t);
static void free_internal(struct pnode *);

/*
 * Create a node with no children, mask = high = 0, and the provided slen
 * and s[].
 */
static struct pnode *
node_alloc(uint8_t slen, const uint8_t * s)
{
	struct pnode *	n;

	/* Allocate. */
	if ((n = malloc(sizeof(struct pnode) + slen)) == NULL)
		return (NULL);

	/* No children, mask, or high bits. */
	n->left = n->right = NULL;
	n->mask = n->high = 0;

	/* Insert provided bytes of key. */
	n->slen = slen;
	memcpy(n->s, s, slen);

	/* Success! */
	return (n);
}

/*
 * Create a duplicate of a node but with different slen and s[].
 */
static struct pnode *
node_dup(const struct pnode * n0, uint8_t slen, const uint8_t * s)
{
	struct pnode *	n;

	/* Allocate. */
	if ((n = malloc(sizeof(struct pnode) + slen)) == NULL)
		return (NULL);

	/* Copy children, mask, and high bits. */
	n->left = n0->left;
	n->right = n0->right;
	n->mask = n0->mask;
	n->high = n0->high;

	/* Insert provided bytes of key. */
	n->slen = slen;
	memcpy(n->s, s, slen);

	/* Success! */
	return (n);
}

/*
 * Compare the given key to the given node.  If they match (i.e., the node is
 * a prefix of the key), return zero; otherwise, return non-zero and set the
 * values mlen and mask to the number of matching bytes and the bitmask where
 * there is a mismatch (where mask == 0 means that the key is a prefix of the
 * node).
 */
static int
compare(const struct pnode * n, const uint8_t * key, size_t keylen,
    uint8_t * mlen, uint8_t * mask)
{
	size_t	i;
	uint8_t	mm;

	/* Scan through the complete bytes in the node. */
	for (i = 0; i < n->slen; i++) {
		/* Is the key a prefix of the node? */
		if (keylen == i) {
			*mlen = i;
			*mask = 0;
			return (1);
		}

		/* Compute how the bytes differ. */
		mm = n->s[i] ^ key[i];

		/* If the ith bytes match, move on to the next position. */
		if (mm == 0)
			continue;

		/* Figure out which bit they mismatch at. */
		for (*mask = 0x80; *mask != 0; *mask >>= 1) {
			if (mm & *mask)
				break;
		}

		/* There are i matching bytes. */
		*mlen = i;

		/* The key doesn't match the node. */
		return (1);
	}

	/* If the node splits on the 9th bit, it is a prefix of the key. */
	if (n->mask == 0)
		return (0);

	/* Otherwise, consider the high bits stored in the node. */

	/* Is the key a prefix of the node? */
	if (keylen == n->slen) {
		*mlen = n->slen;
		*mask = 0;
		return (1);
	}

	/* Compute how the top bits differ. */
	mm = (n->high ^ key[i]) & ((- n->mask) << 1);

	/* If the top bits match, the node is a prefix of the key. */
	if (mm == 0)
		return (0);

	/* Figure out which bit they mismatch at. */
	for (*mask = 0x80; *mask != 0; *mask >>= 1) {
		if (mm & *mask)
			break;
	}

	/* There are n->slen matching bytes. */
	*mlen = n->slen;

	/* The key doesn't match this node. */
	return (1);
}

/*
 * Recursively call func(cookie, key, keylen, rec) on all records under the
 * node n; the first keypos bytes of keybuf hold the key prefix generated
 * from ancestor nodes.
 */
static int
foreach_internal(struct pnode * n,
    int func(void *, uint8_t *, size_t, void *),
    void * cookie, uint8_t * keybuf, size_t keypos)
{
	int	rc = 0;

	/* Add bytes to the key buffer. */
	memcpy(keybuf + keypos, n->s, n->slen);
	keypos += n->slen;

	/* Left child. */
	if (n->left != NULL) {
		if (n->mask == 0) {
			rc = func(cookie, keybuf, keypos, n->left);
		} else {
			rc = foreach_internal(n->left, func, cookie,
			    keybuf, keypos);
		}
	}

	/* Return non-zero status if necessary. */
	if (rc)
		return (rc);

	/* Right child. */
	if (n->right != NULL)
		rc = foreach_internal(n->right, func, cookie, keybuf, keypos);

	/* Return status. */
	return (rc);
}

/*
 * Recursively free the tree.
 */
static void
free_internal(struct pnode * n)
{

	/* Left child. */
	if ((n->mask != 0) && (n->left != NULL))
		free_internal(n->left);

	/* Right child. */
	if (n->right != NULL)
		free_internal(n->right);

	/* Free this node. */
	free(n);
}

/**
 * patricia_init(void):
 * Create a Patricia tree to be used for mapping arbitrary-length keys to
 * records.  Return NULL on failure.
 */
PATRICIA *
patricia_init(void)
{
	PATRICIA * P;

	/* Allocate memory, or return failure. */
	if ((P = malloc(sizeof(PATRICIA))) == NULL)
		return (NULL);

	/* All keys so far have zero length, and we have no nodes. */
	P->root = NULL;
	P->maxkey = 0;

	/* Success! */
	return (P);
}

/**
 * patricia_insert(tree, key, keylen, rec):
 * Associate the provided key of length keylen bytes with the pointer rec,
 * which must be non-NULL.  Return (-1) on error, 0 on success, and 1 if the
 * key already exists.
 */
int
patricia_insert(PATRICIA * P, const uint8_t * key, size_t keylen, void * rec)
{
	struct pnode **	np = &P->root;
	struct pnode *	pnew;
	struct pnode *	pnew2;
	size_t		slen;
	uint8_t		mlen;
	uint8_t		mask;

	/* Update maximum key length. */
	if (P->maxkey < keylen)
		P->maxkey = keylen;

	/*
	 * To understand this code, first read the code for patricia_lookup.
	 * This follows essentially the same approach, except that we keep
	 * an extra level of indirection so that we can insert a new node
	 * into the tree _above_ the node which we are considering at any
	 * particular point.
	 */
	do {
		/* Have we fallen off the bottom of the tree? */
		if (*np == NULL) {
			/*
			 * Create a new node with up to MAXSLEN bytes of the
			 * key, and add it at the current point.  Then keep
			 * on going (and move down into the newly added node).
			 */
			/* Figure out how much key goes into this node. */
			slen = keylen;
			if (slen > MAXSLEN)
				slen = MAXSLEN;

			/* Create the node or error out. */
			if ((pnew = node_alloc(slen, key)) == NULL)
				return (-1);

			/* Add the new node into the tree. */
			*np = pnew;
		}

		/* Is the node not a prefix of the key? */
		if (compare(*np, key, keylen, &mlen, &mask)) {
			/*
			 * Split the node *np after mlen bytes and a number
			 * of bits based on mask.  Leave *np pointing to the
			 * upper of the two nodes (because we will continue
			 * by traversing into the so-far-nonexistent child
			 * of the new node).
			 */
			/* Create the lower of the new nodes. */
			slen = (*np)->slen - mlen;
			pnew2 = node_dup(*np, slen, (*np)->s + mlen);
			if (pnew2 == NULL)
				return (-1);

			/* Create the upper of the new nodes. */
			if ((pnew = node_alloc(mlen, key)) == NULL) {
				free(pnew2);
				return (-1);
			}
			pnew->mask = mask;

			/* Handle splitting on bit 9 differently. */
			if (mask == 0) {
				pnew->high = 0;
				pnew->right = pnew2;
			} else {
				pnew->high = key[mlen] & ((- mask) << 1);

				/*
				 * This looks wrong, but it actually works:
				 * mask is the bit where key[mlen] and
				 * (*np)->s[mlen] differ, so if key[mlen]
				 * has a 1 bit, (*np)->s[mlen] has a 0 bit
				 * and belongs on the left (and vice versa).
				 */
				if (key[mlen] & mask)
					pnew->left = pnew2;
				else
					pnew->right = pnew2;
			}

			/* Free the node which we are replacing. */
			free(*np);

			/* Reattach this branch to the tree. */
			*np = pnew;
		}

		/* Strip off the matching part of the key. */
		key += (*np)->slen;
		keylen -= (*np)->slen;

		/* Handle splitting on the 9th bit specially. */
		if ((*np)->mask == 0) {
			/* Have we found the key? */
			if (keylen == 0) {
				/* Add the record or return 1. */
				if ((*np)->left == NULL) {
					(*np)->left = rec;
					return (0);
				} else {
					return (1);
				}
			}

			/* The key continues; traverse to right child. */
			np = &(*np)->right;
			continue;
		}

		/* Take left or right child depending upon critical bit. */
		if (key[0] & (*np)->mask)
			np = &(*np)->right;
		else
			np = &(*np)->left;
	} while (1);
}

/**
 * patricia_lookup(tree, key, keylen):
 * Look up the provided key of length keylen bytes.  Return a pointer to the
 * associated _record pointer_ if the key is present in the tree (this can
 * be used to change the record pointer associated with the key); or NULL
 * otherwise.
 *
 * Note that a record can be deleted from a Patricia tree as follows:
 * void ** recp = patricia_lookup(tree, key, keylen);
 * if (recp != NULL)
 *     *recp = NULL;
 * but this does not reduce the memory used by the tree as one might expect
 * from reducing its size.
 */
void **
patricia_lookup(PATRICIA * P, const uint8_t * key, size_t keylen)
{
	struct pnode *	n = P->root;
	uint8_t		t0, t1;		/* Garbage variables. */

	/* Traverse the tree until we find the key or give up. */
	do {
		/* Have we fallen off the bottom of the tree? */
		if (n == NULL)
			return (NULL);

		/* Is the node not a prefix of the key? */
		if (compare(n, key, keylen, &t0, &t1))
			return (NULL);

		/* Strip off the matching part of the key. */
		key += n->slen;
		keylen -= n->slen;

		/* Handle splitting on the 9th bit specially. */
		if (n->mask == 0) {
			/* Have we found the key? */
			if (keylen == 0) {
				/* Is there a record here? */
				if (n->left != NULL)
					return ((void **)(void *)&n->left);
				else
					return (NULL);
			}

			/* The key continues; traverse to right child. */
			n = n->right;
			continue;
		}

		/* Take left or right child depending upon critical bit. */
		if (key[0] & n->mask)
			n = n->right;
		else
			n = n->left;
	} while (1);
}

/**
 * patricia_foreach(tree, func, cookie):
 * Traverse the tree in lexicographical order of stored keys, and call
 * func(cookie, key, keylen, rec) for each (key, record) pair.  Stop the
 * traversal early if func returns a non-zero value; return zero, the
 * non-zero value returned by func, or (-1) if an error occurs in the
 * tree traversal.
 */
int
patricia_foreach(PATRICIA * P, int func(void *, uint8_t *, size_t, void *),
    void * cookie)
{
	uint8_t *	keybuf;
	int		rc;

	/* Allocate buffer to store keys generated during traversal. */
	keybuf = malloc(P->maxkey);
	if ((keybuf == NULL) && (P->maxkey > 0))
		return (-1);

	/* Call a recursive function to do all the work. */
	if (P->root != NULL)
		rc = foreach_internal(P->root, func, cookie, keybuf, 0);
	else
		rc = 0;

	/* Free temporary buffer. */
	free(keybuf);

	/* Return status from func calls. */
	return (rc);
}

/**
 * patricia_free(tree):
 * Free the tree.
 */
void
patricia_free(PATRICIA * P)
{

	/* Behave consistently with free(NULL). */
	if (P == NULL)
		return;

	/* Call a recursive function to free all the nodes. */
	if (P->root != NULL)
		free_internal(P->root);

	/* Free the tree structure. */
	free(P);
}
