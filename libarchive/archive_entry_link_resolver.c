/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD: src/lib/libarchive/archive_entry_link_resolver.c,v 1.4 2008/09/05 06:15:25 kientzle Exp $");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_entry.h"

/*
 * This is mostly a pretty straightforward hash table implementation.
 * The only interesting bit is the different strategies used to
 * match up links.  These strategies match those used by various
 * archiving formats:
 *   tar - content stored with first link, remainder refer back to it.
 *       This requires us to match each subsequent link up with the
 *       first appearance.
 *   cpio - Old cpio just stored body with each link, match-ups were
 *       implicit.  This is trivial.
 *   new cpio - New cpio only stores body with last link, match-ups
 *       are implicit.  This is actually quite tricky; see the notes
 *       below.
 */

/* Users pass us a format code, we translate that into a strategy here. */
#define ARCHIVE_ENTRY_LINKIFY_LIKE_TAR	0
#define ARCHIVE_ENTRY_LINKIFY_LIKE_MTREE 1
#define ARCHIVE_ENTRY_LINKIFY_LIKE_OLD_CPIO 2
#define ARCHIVE_ENTRY_LINKIFY_LIKE_NEW_CPIO 3

/* Initial size of link cache. */
#define	links_cache_initial_size 1024

struct links_entry {
	struct links_entry	*next;
	struct links_entry	*previous;
	int			 links; /* # links not yet seen */
	int			 hash;
	struct archive_entry	*canonical;
	struct archive_entry	*entry;
};

struct archive_entry_linkresolver {
	struct links_entry	**buckets;
	struct links_entry	 *spare;
	unsigned long		  number_entries;
	size_t			  number_buckets;
	int			  strategy;
};

static struct links_entry *find_entry(struct archive_entry_linkresolver *,
		    struct archive_entry *);
static void grow_hash(struct archive_entry_linkresolver *);
static struct links_entry *insert_entry(struct archive_entry_linkresolver *,
		    struct archive_entry *);
static struct links_entry *next_entry(struct archive_entry_linkresolver *);

struct archive_entry_linkresolver *
archive_entry_linkresolver_new(void)
{
	struct archive_entry_linkresolver *res;
	size_t i;

	res = malloc(sizeof(struct archive_entry_linkresolver));
	if (res == NULL)
		return (NULL);
	memset(res, 0, sizeof(struct archive_entry_linkresolver));
	res->spare = NULL;
	res->number_buckets = links_cache_initial_size;
	res->buckets = malloc(res->number_buckets *
	    sizeof(res->buckets[0]));
	if (res->buckets == NULL) {
		free(res);
		return (NULL);
	}
	for (i = 0; i < res->number_buckets; i++)
		res->buckets[i] = NULL;
	return (res);
}

void
archive_entry_linkresolver_set_strategy(struct archive_entry_linkresolver *res,
    int fmt)
{
	int fmtbase = fmt & ARCHIVE_FORMAT_BASE_MASK;

	switch (fmtbase) {
	case ARCHIVE_FORMAT_CPIO:
		switch (fmt) {
		case ARCHIVE_FORMAT_CPIO_SVR4_NOCRC:
		case ARCHIVE_FORMAT_CPIO_SVR4_CRC:
			res->strategy = ARCHIVE_ENTRY_LINKIFY_LIKE_NEW_CPIO;
			break;
		default:
			res->strategy = ARCHIVE_ENTRY_LINKIFY_LIKE_OLD_CPIO;
			break;
		}
		break;
	case ARCHIVE_FORMAT_MTREE:
		res->strategy = ARCHIVE_ENTRY_LINKIFY_LIKE_MTREE;
		break;
	case ARCHIVE_FORMAT_TAR:
		res->strategy = ARCHIVE_ENTRY_LINKIFY_LIKE_TAR;
		break;
	default:
		res->strategy = ARCHIVE_ENTRY_LINKIFY_LIKE_TAR;
		break;
	}
}

void
archive_entry_linkresolver_free(struct archive_entry_linkresolver *res)
{
	struct links_entry *le;

	if (res == NULL)
		return;

	if (res->buckets != NULL) {
		while ((le = next_entry(res)) != NULL)
			archive_entry_free(le->entry);
		free(res->buckets);
		res->buckets = NULL;
	}
	free(res);
}

void
archive_entry_linkify(struct archive_entry_linkresolver *res,
    struct archive_entry **e, struct archive_entry **f)
{
	struct links_entry *le;
	struct archive_entry *t;

	*f = NULL; /* Default: Don't return a second entry. */

	if (*e == NULL) {
		le = next_entry(res);
		if (le != NULL) {
			*e = le->entry;
			le->entry = NULL;
		}
		return;
	}

	/* If it has only one link, then we're done. */
	if (archive_entry_nlink(*e) == 1)
		return;
	/* Directories never have hardlinks. */
	if (archive_entry_filetype(*e) == AE_IFDIR)
		return;

	switch (res->strategy) {
	case ARCHIVE_ENTRY_LINKIFY_LIKE_TAR:
		le = find_entry(res, *e);
		if (le != NULL) {
			archive_entry_unset_size(*e);
			archive_entry_copy_hardlink(*e,
			    archive_entry_pathname(le->canonical));
		} else
			insert_entry(res, *e);
		return;
	case ARCHIVE_ENTRY_LINKIFY_LIKE_MTREE:
		le = find_entry(res, *e);
		if (le != NULL) {
			archive_entry_copy_hardlink(*e,
			    archive_entry_pathname(le->canonical));
		} else
			insert_entry(res, *e);
		return;
	case ARCHIVE_ENTRY_LINKIFY_LIKE_OLD_CPIO:
		/* This one is trivial. */
		return;
	case ARCHIVE_ENTRY_LINKIFY_LIKE_NEW_CPIO:
		le = find_entry(res, *e);
		if (le != NULL) {
			/*
			 * Put the new entry in le, return the
			 * old entry from le.
			 */
			t = *e;
			*e = le->entry;
			le->entry = t;
			/* Make the old entry into a hardlink. */
			archive_entry_unset_size(*e);
			archive_entry_copy_hardlink(*e,
			    archive_entry_pathname(le->canonical));
			/* If we ran out of links, return the
			 * final entry as well. */
			if (le->links == 0) {
				*f = le->entry;
				le->entry = NULL;
			}
		} else {
			/*
			 * If we haven't seen it, tuck it away
			 * for future use.
			 */
			le = insert_entry(res, *e);
			le->entry = *e;
			*e = NULL;
		}
		return;
	default:
		break;
	}
	return;
}

static struct links_entry *
find_entry(struct archive_entry_linkresolver *res,
    struct archive_entry *entry)
{
	struct links_entry	*le;
	int			 hash, bucket;
	dev_t			 dev;
	ino_t			 ino;

	/* Free a held entry. */
	if (res->spare != NULL) {
		archive_entry_free(res->spare->canonical);
		archive_entry_free(res->spare->entry);
		free(res->spare);
		res->spare = NULL;
	}

	/* If the links cache overflowed and got flushed, don't bother. */
	if (res->buckets == NULL)
		return (NULL);

	dev = archive_entry_dev(entry);
	ino = archive_entry_ino(entry);
	hash = dev ^ ino;

	/* Try to locate this entry in the links cache. */
	bucket = hash % res->number_buckets;
	for (le = res->buckets[bucket]; le != NULL; le = le->next) {
		if (le->hash == hash
		    && dev == archive_entry_dev(le->canonical)
		    && ino == archive_entry_ino(le->canonical)) {
			/*
			 * Decrement link count each time and release
			 * the entry if it hits zero.  This saves
			 * memory and is necessary for detecting
			 * missed links.
			 */
			--le->links;
			if (le->links > 0)
				return (le);
			/* Remove it from this hash bucket. */
			if (le->previous != NULL)
				le->previous->next = le->next;
			if (le->next != NULL)
				le->next->previous = le->previous;
			if (res->buckets[bucket] == le)
				res->buckets[bucket] = le->next;
			res->number_entries--;
			/* Defer freeing this entry. */
			res->spare = le;
			return (le);
		}
	}
	return (NULL);
}

static struct links_entry *
next_entry(struct archive_entry_linkresolver *res)
{
	struct links_entry	*le;
	size_t			 bucket;

	/* Free a held entry. */
	if (res->spare != NULL) {
		archive_entry_free(res->spare->canonical);
		free(res->spare);
		res->spare = NULL;
	}

	/* If the links cache overflowed and got flushed, don't bother. */
	if (res->buckets == NULL)
		return (NULL);

	/* Look for next non-empty bucket in the links cache. */
	for (bucket = 0; bucket < res->number_buckets; bucket++) {
		le = res->buckets[bucket];
		if (le != NULL) {
			/* Remove it from this hash bucket. */
			if (le->next != NULL)
				le->next->previous = le->previous;
			res->buckets[bucket] = le->next;
			res->number_entries--;
			/* Defer freeing this entry. */
			res->spare = le;
			return (le);
		}
	}
	return (NULL);
}

static struct links_entry *
insert_entry(struct archive_entry_linkresolver *res,
    struct archive_entry *entry)
{
	struct links_entry *le;
	int			 hash, bucket;

	/* Add this entry to the links cache. */
	le = malloc(sizeof(struct links_entry));
	if (le == NULL)
		return (NULL);
	memset(le, 0, sizeof(*le));
	le->canonical = archive_entry_clone(entry);

	/* If the links cache is getting too full, enlarge the hash table. */
	if (res->number_entries > res->number_buckets * 2)
		grow_hash(res);

	hash = archive_entry_dev(entry) ^ archive_entry_ino(entry);
	bucket = hash % res->number_buckets;

	/* If we could allocate the entry, record it. */
	if (res->buckets[bucket] != NULL)
		res->buckets[bucket]->previous = le;
	res->number_entries++;
	le->next = res->buckets[bucket];
	le->previous = NULL;
	res->buckets[bucket] = le;
	le->hash = hash;
	le->links = archive_entry_nlink(entry) - 1;
	return (le);
}

static void
grow_hash(struct archive_entry_linkresolver *res)
{
	struct links_entry *le, **new_buckets;
	size_t new_size;
	size_t i, bucket;

	/* Try to enlarge the bucket list. */
	new_size = res->number_buckets * 2;
	new_buckets = malloc(new_size * sizeof(struct links_entry *));

	if (new_buckets != NULL) {
		memset(new_buckets, 0,
		    new_size * sizeof(struct links_entry *));
		for (i = 0; i < res->number_buckets; i++) {
			while (res->buckets[i] != NULL) {
				/* Remove entry from old bucket. */
				le = res->buckets[i];
				res->buckets[i] = le->next;

				/* Add entry to new bucket. */
				bucket = le->hash % new_size;

				if (new_buckets[bucket] != NULL)
					new_buckets[bucket]->previous =
					    le;
				le->next = new_buckets[bucket];
				le->previous = NULL;
				new_buckets[bucket] = le;
			}
		}
		free(res->buckets);
		res->buckets = new_buckets;
		res->number_buckets = new_size;
	}
}
