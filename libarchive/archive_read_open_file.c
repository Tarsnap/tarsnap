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
__FBSDID("$FreeBSD: src/lib/libarchive/archive_read_open_file.c,v 1.20 2007/06/26 03:06:48 kientzle Exp $");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
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

struct read_FILE_data {
	FILE    *f;
	size_t	 block_size;
	void	*buffer;
	char	 can_skip;
};

static int	file_close(struct archive *, void *);
static ssize_t	file_read(struct archive *, void *, const void **buff);
#if ARCHIVE_API_VERSION < 2
static ssize_t	file_skip(struct archive *, void *, size_t request);
#else
static off_t	file_skip(struct archive *, void *, off_t request);
#endif

int
archive_read_open_FILE(struct archive *a, FILE *f)
{
	struct stat st;
	struct read_FILE_data *mine;
	size_t block_size = 128 * 1024;
	void *b;

	mine = (struct read_FILE_data *)malloc(sizeof(*mine));
	b = malloc(block_size);
	if (mine == NULL || b == NULL) {
		archive_set_error(a, ENOMEM, "No memory");
		free(mine);
		free(b);
		return (ARCHIVE_FATAL);
	}
	mine->block_size = block_size;
	mine->buffer = b;
	mine->f = f;
	/*
	 * If we can't fstat() the file, it may just be that it's not
	 * a file.  (FILE * objects can wrap many kinds of I/O
	 * streams, some of which don't support fileno()).)
	 */
	if (fstat(fileno(mine->f), &st) == 0 && S_ISREG(st.st_mode)) {
		archive_read_extract_set_skip_file(a, st.st_dev, st.st_ino);
		/* Enable the seek optimization only for regular files. */
		mine->can_skip = 1;
	} else
		mine->can_skip = 0;

	return (archive_read_open2(a, mine, NULL, file_read,
		    file_skip, file_close));
}

static ssize_t
file_read(struct archive *a, void *client_data, const void **buff)
{
	struct read_FILE_data *mine = (struct read_FILE_data *)client_data;
	size_t bytes_read;

	*buff = mine->buffer;
	bytes_read = fread(mine->buffer, 1, mine->block_size, mine->f);
	if (bytes_read < mine->block_size && ferror(mine->f)) {
		archive_set_error(a, errno, "Error reading file");
	}
	return (bytes_read);
}

#if ARCHIVE_API_VERSION < 2
static ssize_t
file_skip(struct archive *a, void *client_data, size_t request)
#else
static off_t
file_skip(struct archive *a, void *client_data, off_t request)
#endif
{
	struct read_FILE_data *mine = (struct read_FILE_data *)client_data;

	(void)a; /* UNUSED */

	/*
	 * If we can't skip, return 0 as the amount we did step and
	 * the caller will work around by reading and discarding.
	 */
	if (!mine->can_skip)
		return (0);
	if (request == 0)
		return (0);

#if HAVE_FSEEKO
	if (fseeko(mine->f, request, SEEK_CUR) != 0)
#else
	if (fseek(mine->f, request, SEEK_CUR) != 0)
#endif
	{
		mine->can_skip = 0;
		return (0);
	}
	return (request);
}

static int
file_close(struct archive *a, void *client_data)
{
	struct read_FILE_data *mine = (struct read_FILE_data *)client_data;

	(void)a; /* UNUSED */
	if (mine->buffer != NULL)
		free(mine->buffer);
	free(mine);
	return (ARCHIVE_OK);
}
