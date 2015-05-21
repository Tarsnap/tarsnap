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
 *
 * $FreeBSD: src/lib/libarchive/archive_private.h,v 1.29 2007/04/02 00:15:45 kientzle Exp $
 */

#ifndef ARCHIVE_PRIVATE_H_INCLUDED
#define	ARCHIVE_PRIVATE_H_INCLUDED

#include "archive.h"
#include "archive_string.h"

#define	ARCHIVE_WRITE_MAGIC	(0xb0c5c0deU)
#define	ARCHIVE_READ_MAGIC	(0xdeb0c5U)
#define ARCHIVE_WRITE_DISK_MAGIC (0xc001b0c5U)

#define	ARCHIVE_STATE_ANY	0xFFFFU
#define	ARCHIVE_STATE_NEW	1U
#define	ARCHIVE_STATE_HEADER	2U
#define	ARCHIVE_STATE_DATA	4U
#define ARCHIVE_STATE_DATA_END	8U
#define	ARCHIVE_STATE_EOF	0x10U
#define	ARCHIVE_STATE_CLOSED	0x20U
#define	ARCHIVE_STATE_FATAL	0x8000U

struct archive_vtable {
	int	(*archive_write_close)(struct archive *);
	int	(*archive_write_finish)(struct archive *);
	int	(*archive_write_header)(struct archive *,
	    struct archive_entry *);
	int	(*archive_write_finish_entry)(struct archive *);
	ssize_t	(*archive_write_data)(struct archive *,
	    const void *, size_t);
	ssize_t	(*archive_write_data_block)(struct archive *,
	    const void *, size_t, off_t);
};

struct archive {
	/*
	 * The magic/state values are used to sanity-check the
	 * client's usage.  If an API function is called at a
	 * ridiculous time, or the client passes us an invalid
	 * pointer, these values allow me to catch that.
	 */
	unsigned int	magic;
	unsigned int	state;

	/*
	 * Some public API functions depend on the "real" type of the
	 * archive object.
	 */
	struct archive_vtable *vtable;

	int		  archive_format;
	const char	 *archive_format_name;

	int	  compression_code;	/* Currently active compression. */
	const char *compression_name;

	/* Position in UNCOMPRESSED data stream. */
	off_t		  file_position;
	/* Position in COMPRESSED data stream. */
	off_t		  raw_position;

	int		  archive_error_number;
	const char	 *error;
	struct archive_string	error_string;
};

/* Check magic value and state; exit if it isn't valid. */
void	__archive_check_magic(struct archive *, unsigned int magic,
	    unsigned int state, const char *func);

void	__archive_errx(int retvalue, const char *msg);

#define	err_combine(a,b)	((a) < (b) ? (a) : (b))

#endif
