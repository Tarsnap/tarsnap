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

/*
 * This file contains the "essential" portions of the read API, that
 * is, stuff that will probably always be used by any client that
 * actually needs to read an archive.  Optional pieces have been, as
 * far as possible, separated out into separate files to avoid
 * needlessly bloating statically-linked clients.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD: src/lib/libarchive/archive_read.c,v 1.39 2008/12/06 06:45:15 kientzle Exp $");

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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"
#include "archive_read_private.h"

#define minimum(a, b) (a < b ? a : b)

static int	build_stream(struct archive_read *);
static int	choose_format(struct archive_read *);
static struct archive_vtable *archive_read_vtable(void);
static int	_archive_read_close(struct archive *);
static int	_archive_read_finish(struct archive *);

static struct archive_vtable *
archive_read_vtable(void)
{
	static struct archive_vtable av;
	static int inited = 0;

	if (!inited) {
		av.archive_finish = _archive_read_finish;
		av.archive_close = _archive_read_close;
		inited = 1;
	}
	return (&av);
}

/*
 * Allocate, initialize and return a struct archive object.
 */
struct archive *
archive_read_new(void)
{
	struct archive_read *a;

	a = (struct archive_read *)malloc(sizeof(*a));
	if (a == NULL)
		return (NULL);
	memset(a, 0, sizeof(*a));
	a->archive.magic = ARCHIVE_READ_MAGIC;

	a->archive.state = ARCHIVE_STATE_NEW;
	a->entry = archive_entry_new();
	a->archive.vtable = archive_read_vtable();

	return (&a->archive);
}

/*
 * Record the do-not-extract-to file. This belongs in archive_read_extract.c.
 */
void
archive_read_extract_set_skip_file(struct archive *_a, dev_t d, ino_t i)
{
	struct archive_read *a = (struct archive_read *)_a;
	__archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_ANY,
	    "archive_read_extract_set_skip_file");
	a->skip_file_dev = d;
	a->skip_file_ino = i;
}

/*
 * Set read options for the format.
 */
int
archive_read_set_format_options(struct archive *_a, const char *s)
{
	struct archive_read *a;
	struct archive_format_descriptor *format;
	char key[64], val[64];
	size_t i;
	int len, r;

	if (s == NULL || *s == '\0')
		return (ARCHIVE_OK);
	a = (struct archive_read *)_a;
	__archive_check_magic(&a->archive, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_set_format_options");
	len = 0;
	for (i = 0; i < sizeof(a->formats)/sizeof(a->formats[0]); i++) {
		format = &a->formats[i];
		if (format == NULL || format->options == NULL ||
		    format->name == NULL)
			/* This format does not support option. */
			continue;

		while ((len = __archive_parse_options(s, format->name,
		    sizeof(key), key, sizeof(val), val)) > 0) {
			if (val[0] == '\0')
				r = format->options(a, key, NULL);
			else
				r = format->options(a, key, val);
			if (r == ARCHIVE_FATAL)
				return (r);
			s += len;
		}
	}
	if (len < 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Illegal format options.");
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

/*
 * Set read options for the filter.
 */
int
archive_read_set_filter_options(struct archive *_a, const char *s)
{
	struct archive_read *a;
	struct archive_read_filter *filter;
	struct archive_read_filter_bidder *bidder;
	char key[64], val[64];
	int len, r;

	if (s == NULL || *s == '\0')
		return (ARCHIVE_OK);
	a = (struct archive_read *)_a;
	__archive_check_magic(&a->archive, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_set_filter_options");
	len = 0;
	for (filter = a->filter; filter != NULL; filter = filter->upstream) {
		bidder = filter->bidder;
		if (bidder == NULL)
			continue;
		if (bidder->options == NULL)
			/* This bidder does not support option */
			continue;
		while ((len = __archive_parse_options(s, filter->name,
		    sizeof(key), key, sizeof(val), val)) > 0) {
			if (val[0] == '\0')
				r = bidder->options(bidder, key, NULL);
			else
				r = bidder->options(bidder, key, val);
			if (r == ARCHIVE_FATAL)
				return (r);
			s += len;
		}
	}
	if (len < 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Illegal format options.");
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

/*
 * Set read options for the format and the filter.
 */
int
archive_read_set_options(struct archive *_a, const char *s)
{
	int r;

	r = archive_read_set_format_options(_a, s);
	if (r != ARCHIVE_OK)
		return (r);
	r = archive_read_set_filter_options(_a, s);
	if (r != ARCHIVE_OK)
		return (r);
	return (ARCHIVE_OK);
}

/*
 * Open the archive
 */
int
archive_read_open(struct archive *a, void *client_data,
    archive_open_callback *client_opener, archive_read_callback *client_reader,
    archive_close_callback *client_closer)
{
	/* Old archive_read_open() is just a thin shell around
	 * archive_read_open2. */
	return archive_read_open2(a, client_data, client_opener,
	    client_reader, NULL, client_closer);
}

static ssize_t
client_read_proxy(struct archive_read_filter *self, const void **buff)
{
	ssize_t r;
	r = (self->archive->client.reader)(&self->archive->archive,
	    self->data, buff);
	self->archive->archive.raw_position += r;
	return (r);
}

static int64_t
client_skip_proxy(struct archive_read_filter *self, int64_t request)
{
	int64_t r;
	if (self->archive->client.skipper == NULL)
		return (0);
	r = (self->archive->client.skipper)(&self->archive->archive,
	    self->data, request);
	self->archive->archive.raw_position += r;
	return (r);
}

static int
client_close_proxy(struct archive_read_filter *self)
{
	int r = ARCHIVE_OK;

	if (self->archive->client.closer != NULL)
		r = (self->archive->client.closer)((struct archive *)self->archive,
		    self->data);
	self->data = NULL;
	return (r);
}


int
archive_read_open2(struct archive *_a, void *client_data,
    archive_open_callback *client_opener,
    archive_read_callback *client_reader,
    archive_skip_callback *client_skipper,
    archive_close_callback *client_closer)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct archive_read_filter *filter;
	int e;

	__archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_open");

	if (client_reader == NULL)
		__archive_errx(1,
		    "No reader function provided to archive_read_open");

	/* Open data source. */
	if (client_opener != NULL) {
		e =(client_opener)(&a->archive, client_data);
		if (e != 0) {
			/* If the open failed, call the closer to clean up. */
			if (client_closer)
				(client_closer)(&a->archive, client_data);
			return (e);
		}
	}

	/* Save the client functions and mock up the initial source. */
	a->client.reader = client_reader;
	a->client.skipper = client_skipper;
	a->client.closer = client_closer;

	filter = calloc(1, sizeof(*filter));
	if (filter == NULL)
		return (ARCHIVE_FATAL);
	filter->bidder = NULL;
	filter->upstream = NULL;
	filter->archive = a;
	filter->data = client_data;
	filter->read = client_read_proxy;
	filter->skip = client_skip_proxy;
	filter->close = client_close_proxy;
	filter->name = "none";
	filter->code = ARCHIVE_COMPRESSION_NONE;
	a->filter = filter;

	/* Build out the input pipeline. */
	e = build_stream(a);
	if (e == ARCHIVE_OK)
		a->archive.state = ARCHIVE_STATE_HEADER;

	return (e);
}

/*
 * Allow each registered stream transform to bid on whether
 * it wants to handle this stream.  Repeat until we've finished
 * building the pipeline.
 */
static int
build_stream(struct archive_read *a)
{
	int number_bidders, i, bid, best_bid;
	struct archive_read_filter_bidder *bidder, *best_bidder;
	struct archive_read_filter *filter;
	int r;
	int filter_count = 0;
	#define MAX_FILTERS 16

	for (;;) {
		number_bidders = sizeof(a->bidders) / sizeof(a->bidders[0]);

		best_bid = 0;
		best_bidder = NULL;

		bidder = a->bidders;
		for (i = 0; i < number_bidders; i++, bidder++) {
			if (bidder->bid != NULL) {
				bid = (bidder->bid)(bidder, a->filter);
				if (bid > best_bid) {
					best_bid = bid;
					best_bidder = bidder;
				}
			}
		}

		/* If no bidder, we're done. */
		if (best_bidder == NULL) {
			a->archive.compression_name = a->filter->name;
			a->archive.compression_code = a->filter->code;
			return (ARCHIVE_OK);
		}

		if (filter_count >= MAX_FILTERS) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "Input requires too many filters for decoding");
			return (ARCHIVE_FATAL);
		}

		filter
		    = (struct archive_read_filter *)calloc(1, sizeof(*filter));
		if (filter == NULL)
			return (ARCHIVE_FATAL);
		filter->bidder = best_bidder;
		filter->archive = a;
		filter->upstream = a->filter;
		r = (best_bidder->init)(filter);
		if (r != ARCHIVE_OK) {
			free(filter);
			return (r);
		}
		a->filter = filter;
		filter_count++;
	}
}

/*
 * Read header of next entry.
 */
int
