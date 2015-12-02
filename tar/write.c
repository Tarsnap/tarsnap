/*-
 * Copyright 2006-2009 Colin Percival
 * All rights reserved.
 *
 * Portions of the file below are covered by the following license:
 *
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

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD: src/usr.bin/tar/write.c,v 1.79 2008/11/27 05:49:52 kientzle Exp $");

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_ACL_H
#include <sys/acl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif
#ifdef HAVE_GRP_H
#include <grp.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>	/* for Linux file flags */
#endif
/*
 * Some Linux distributions have both linux/ext2_fs.h and ext2fs/ext2_fs.h.
 * As the include guards don't agree, the order of include is important.
 */
#ifdef HAVE_LINUX_EXT2_FS_H
#include <linux/ext2_fs.h>	/* for Linux file flags */
#endif
#if defined(HAVE_EXT2FS_EXT2_FS_H) && !defined(__CYGWIN__)
/* This header exists but is broken on Cygwin. */
#include <ext2fs/ext2_fs.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <signal.h>
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "bsdtar.h"
#include "tree.h"

#include "archive_multitape.h"
#include "ccache.h"
#include "getfstype.h"
#include "sigquit.h"
#include "tsnetwork.h"

/* Size of buffer for holding file data prior to writing. */
#define FILEDATABUFLEN	65536

static int		 append_archive(struct bsdtar *, struct archive *,
			     struct archive *ina, void * cookie);
static int		 append_archive_filename(struct bsdtar *,
			     struct archive *, const char *fname);
static int		 append_archive_tarsnap(struct bsdtar *,
			     struct archive *, const char *tapename);
static void		 archive_names_from_file(struct bsdtar *bsdtar,
			     struct archive *a);
static int		 archive_names_from_file_helper(struct bsdtar *bsdtar,
			     const char *line);
static int		 copy_file_data(struct bsdtar *bsdtar,
			     struct archive *a, struct archive *ina);
static int		 getdevino(struct archive *, const char *, dev_t *,
			     ino_t *);
static int		 new_enough(struct bsdtar *, const char *path,
			     const struct stat *);
static int		 truncate_archive(struct bsdtar *);
static void		 write_archive(struct archive *, struct bsdtar *);
static void		 write_entry_backend(struct bsdtar *, struct archive *,
			     struct archive_entry *,
			     const struct stat *, const char *);
static int		 write_file_data(struct bsdtar *, struct archive *,
			     struct archive_entry *, int fd);
static void		 write_hierarchy(struct bsdtar *, struct archive *,
			     const char *);

/*
 * Macros to simplify mode-switching.
 */
#define MODE_SET(bsdtar, a, mode)					\
	archive_write_multitape_setmode(a, bsdtar->write_cookie, mode)
#define MODE_HEADER(bsdtar, a)	MODE_SET(bsdtar, a, 0)
#define MODE_DATA(bsdtar, a)	MODE_SET(bsdtar, a, 1)
#define MODE_DONE(bsdtar, a)						\
	(archive_write_finish_entry(a) ||				\
	    MODE_SET(bsdtar, a, 2))

/* Get the device and inode numbers of a path. */
static int
getdevino(struct archive * a, const char * path, dev_t * d, ino_t * i)
{
	struct stat sb;

	if (path == NULL) {
		/* 
		 * Return bogus values to avoid comparing against
		 * uninitialized data later on.
		 */
		*d = -1;
		*i = -1;
		return 0;
	}

	if (stat(path, &sb)) {
		archive_set_error(a, errno, "%s", path);
		return (-1);
	} else {
		*d = sb.st_dev;
		*i = sb.st_ino;
		return (0);
	}
}

/* Determine if we need to truncate the archive at the current point. */
static int
truncate_archive(struct bsdtar *bsdtar)
{
	static int msgprinted = 0;

	if (sigquit_received == 0)
		return (0);

	/* Tell the user that we got the message. */
	if (msgprinted == 0) {
		bsdtar_warnc(bsdtar, 0, "quit signal received or bandwidth "
		    "limit reached; archive is being truncated");
		msgprinted = 1;
	}

	/* Tell the multitape code to truncate the archive. */
	archive_write_multitape_truncate(bsdtar->write_cookie);

	/* Yes, we need to stop writing now. */
	return (1);
}

/* If the --disk-pause option was used, sleep for a while. */
static void
disk_pause(struct bsdtar *bsdtar)
{
	struct timespec ts;

	/* Do we want to sleep? */
	if (bsdtar->disk_pause == 0)
		return;

	/* Sleep. */
	ts.tv_sec = bsdtar->disk_pause / 1000;
	ts.tv_nsec = bsdtar->disk_pause * 1000000;
	nanosleep(&ts, NULL);
}

static volatile sig_atomic_t sigusr2_received = 0;
static void sigusr2_handler(int sig);

static void
sigusr2_handler(int sig)
{

	(void)sig; /* UNUSED */

	/* SIGUSR2 has been received. */
	sigusr2_received = 1;
}

/* Create a checkpoint in the archive if necessary. */
static int
checkpoint_archive(struct bsdtar *bsdtar, int withinline)
{
	int rc;

	if (sigusr2_received == 0)
		return (0);
	sigusr2_received = 0;

	/* If the user asked for verbosity, be verbose. */
	if (bsdtar->verbose) {
		if (withinline)
			fprintf(stderr, "\n");
		fprintf(stderr, "tarsnap: Creating checkpoint...");
	}

	/* Create the checkpoint. */
	rc = archive_write_multitape_checkpoint(bsdtar->write_cookie);

	/* If the user asked for verbosity, be verbose. */
	if (bsdtar->verbose) {
		if (rc == 0)
			fprintf(stderr, " done.");
		if (!withinline)
			fprintf(stderr, "\n");
	}

	/* Return status code from checkpoint creation. */
	return (rc);
}

void
tarsnap_mode_c(struct bsdtar *bsdtar)
{
	struct archive *a;
	size_t i;

	if (*bsdtar->argv == NULL && bsdtar->names_from_file == NULL)
		bsdtar_errc(bsdtar, 1, 0, "no files or directories specified");

	/* Warn if "--" appears at the beginning of a file or dir to archive. */
	for (i = 0; bsdtar->argv[i] != NULL; i++) {
		if (bsdtar->argv[i][0] == '-' && bsdtar->argv[i][1] == '-') {
			bsdtar_warnc(bsdtar, 0,
			    "List of objects to archive includes '%s'."
			    "  This might not be what you intended.",
			    bsdtar->argv[i]);
			break;
		}
	}

	if ((a = archive_write_new()) == NULL) {
		bsdtar_warnc(bsdtar, ENOMEM, "Cannot allocate memory");
		goto err0;
	}

	/* We only support the pax restricted format. */
	archive_write_set_format_pax_restricted(a);

	/* Set the block size to zero -- we don't want buffering. */
	archive_write_set_bytes_per_block(a, 0);

	/* Open the archive, keeping a cookie for talking to the tape layer. */
	bsdtar->write_cookie = archive_write_open_multitape(a,
	    bsdtar->machinenum, bsdtar->cachedir, bsdtar->tapenames[0],
	    bsdtar->argc_orig, bsdtar->argv_orig,
	    bsdtar->option_print_stats, bsdtar->option_dryrun,
	    bsdtar->creationtime, bsdtar->option_csv_filename);
	if (bsdtar->write_cookie == NULL) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		goto err1;
	}

	/*
	 * Remember the device and inode numbers of the cache directory, so
	 * that we can skip it in write_hierarchy().
	 */
	if (getdevino(a, bsdtar->cachedir,
	    &bsdtar->cachedir_dev, &bsdtar->cachedir_ino)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		goto err1;
	}

	/* If the chunkification cache is enabled, read it. */
	if ((bsdtar->cachecrunch < 2) && (bsdtar->cachedir != NULL)) {
		bsdtar->chunk_cache = ccache_read(bsdtar->cachedir);
		if (bsdtar->chunk_cache == NULL) {
			bsdtar_warnc(bsdtar, errno, "Error reading cache");
			goto err1;
		}
	}

	/*
	 * write_archive(a, bsdtar) calls archive_write_finish(a), so we set
	 * a = NULL to avoid doing this again in err1.
	 */
	write_archive(a, bsdtar);
	a = NULL;

	/*
	 * If this isn't a dry run and we're running with the chunkification
	 * cache enabled, write the cache back to disk.
	 */
	if ((bsdtar->option_dryrun == 0) && (bsdtar->cachecrunch < 2)) {
		if (ccache_write(bsdtar->chunk_cache, bsdtar->cachedir)) {
			bsdtar_warnc(bsdtar, errno, "Error writing cache");
			goto err2;
		}
	}

	/* Free the chunkification cache. */
	if (bsdtar->cachecrunch < 2)
		ccache_free(bsdtar->chunk_cache);

	/* Success! */
	return;

err2:
	ccache_free(bsdtar->chunk_cache);
err1:
	archive_write_finish(a);
err0:
	/* Failure! */
	bsdtar->return_value = 1;
	return;
}

/*
 * Write user-specified files/dirs to opened archive.
 */
static void
write_archive(struct archive *a, struct bsdtar *bsdtar)
{
	struct sigaction sa;
	const char *arg;
	struct archive_entry *entry, *sparse_entry;

	/* We want to catch SIGINFO and SIGUSR1. */
	siginfo_init(bsdtar);

	/* We also want to catch SIGQUIT and ^Q. */
	if (sigquit_init())
		exit(1);

	/* And SIGUSR2, too. */
	sa.sa_handler = sigusr2_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGUSR2, &sa, NULL))
		bsdtar_errc(bsdtar, 1, 0, "cannot install signal handler");

	/* Allocate a buffer for file data. */
	if ((bsdtar->buff = malloc(FILEDATABUFLEN)) == NULL)
		bsdtar_errc(bsdtar, 1, 0, "cannot allocate memory");

	if ((bsdtar->resolver = archive_entry_linkresolver_new()) == NULL)
		bsdtar_errc(bsdtar, 1, 0, "cannot create link resolver");
	archive_entry_linkresolver_set_strategy(bsdtar->resolver,
	    archive_format(a));
	if ((bsdtar->diskreader = archive_read_disk_new()) == NULL)
		bsdtar_errc(bsdtar, 1, 0, "Cannot create read_disk object");
	archive_read_disk_set_standard_lookup(bsdtar->diskreader);

	if (bsdtar->names_from_file != NULL)
		archive_names_from_file(bsdtar, a);

	while (*bsdtar->argv) {
		if (truncate_archive(bsdtar))
			break;
		if (checkpoint_archive(bsdtar, 0))
			exit(1);

		arg = *bsdtar->argv;
		if (arg[0] == '-' && arg[1] == 'C') {
			arg += 2;
			if (*arg == '\0') {
				bsdtar->argv++;
				arg = *bsdtar->argv;
				if (arg == NULL) {
					bsdtar_warnc(bsdtar, 0,
					    "Missing argument for -C");
					bsdtar->return_value = 1;
					goto cleanup;
				}
				if (*arg == '\0') {
					bsdtar_warnc(bsdtar, 0,
					    "Meaningless argument for -C: ''");
					bsdtar->return_value = 1;
					goto cleanup;
				}
			}
			set_chdir(bsdtar, arg);
		} else {
			if (arg[0] != '/' &&
			    (arg[0] != '@' || arg[1] != '/') &&
			    (arg[0] != '@' || arg[1] != '@'))
				do_chdir(bsdtar); /* Handle a deferred -C */
			if (arg[0] == '@' && arg[1] == '@') {
				if (append_archive_tarsnap(bsdtar, a,
				    arg + 2) != 0)
					break;
			} else if (arg[0] == '@') {
				if (append_archive_filename(bsdtar, a,
				    arg + 1) != 0)
					break;
			} else
#if defined(_WIN32) && !defined(__CYGWIN__)
				write_hierarchy_win(bsdtar, a, arg,
				    write_hierarchy);
#else
				write_hierarchy(bsdtar, a, arg);
#endif
		}
		bsdtar->argv++;
	}

	/*
	 * This code belongs to bsdtar and is used when writing archives in
	 * "new cpio" format.  It has no effect in tarsnap, since tarsnap
	 * doesn't write archives in this format; but I'm leaving it in to
	 * make the diffs smaller.
	 */
	entry = NULL;
	archive_entry_linkify(bsdtar->resolver, &entry, &sparse_entry);
	while (entry != NULL) {
		write_entry_backend(bsdtar, a, entry, NULL, NULL);
		archive_entry_free(entry);
		entry = NULL;
		archive_entry_linkify(bsdtar->resolver, &entry, &sparse_entry);
	}

	/*
	 * Check whether the archive is empty; this is unaffected by
	 * deduplication.  Must be done before the archive is closed since
	 * even empty archives end up with nonzero size due to tar EOF blocks.
	 */
	if ((!bsdtar->option_quiet) &&
	    (archive_position_compressed(a) == 0)) {
		bsdtar_warnc(bsdtar, 0, "Warning: Archive contains no files");
	}

	if (archive_write_close(a)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		bsdtar->return_value = 1;
	}

cleanup:
	/* Free file data buffer. */
	free(bsdtar->buff);
	archive_entry_linkresolver_free(bsdtar->resolver);
	bsdtar->resolver = NULL;
	archive_read_finish(bsdtar->diskreader);
	bsdtar->diskreader = NULL;

	if (bsdtar->option_totals && (bsdtar->return_value == 0)) {
		fprintf(stderr, "Total bytes written: " BSDTAR_FILESIZE_PRINTF "\n",
		    (BSDTAR_FILESIZE_TYPE)archive_position_compressed(a));
	}

	archive_write_finish(a);

	/* Restore old SIGINFO + SIGUSR1 handlers. */
	siginfo_done(bsdtar);
}

/*
 * Archive names specified in file.
 *
 * Unless --null was specified, a line containing exactly "-C" will
 * cause the next line to be a directory to pass to chdir().  If
 * --null is specified, then a line "-C" is just another filename.
 */
void
archive_names_from_file(struct bsdtar *bsdtar, struct archive *a)
{
	bsdtar->archive = a;

	bsdtar->next_line_is_dir = 0;
	process_lines(bsdtar, bsdtar->names_from_file,
	    archive_names_from_file_helper, bsdtar->option_null);
	if (bsdtar->next_line_is_dir)
		bsdtar_errc(bsdtar, 1, errno,
		    "Unexpected end of filename list; "
		    "directory expected after -C");
}

static int
archive_names_from_file_helper(struct bsdtar *bsdtar, const char *line)
{
	if (bsdtar->next_line_is_dir) {
		if (*line != '\0')
			set_chdir(bsdtar, line);
		else {
			bsdtar_warnc(bsdtar, 0,
			    "Meaningless argument for -C: ''");
			bsdtar->return_value = 1;
		}
		bsdtar->next_line_is_dir = 0;
	} else if (!bsdtar->option_null && strcmp(line, "-C") == 0)
		bsdtar->next_line_is_dir = 1;
	else {
		if (*line != '/')
			do_chdir(bsdtar); /* Handle a deferred -C */
		write_hierarchy(bsdtar, bsdtar->archive, line);
	}
	return (0);
}

/*
 * Copy from specified archive to current archive.  Returns non-zero
 * for write errors (which force us to terminate the entire archiving
 * operation).  If there are errors reading the input archive, we set
 * bsdtar->return_value but return zero, so the overall archiving
 * operation will complete and return non-zero.
 */
static int
append_archive_filename(struct bsdtar *bsdtar, struct archive *a,
    const char *filename)
{
	struct archive *ina;
	int rc;

	if (strcmp(filename, "-") == 0)
		filename = NULL; /* Library uses NULL for stdio. */

	ina = archive_read_new();
	archive_read_support_format_all(ina);
	archive_read_support_compression_all(ina);
	if (archive_read_open_file(ina, filename, 10240)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(ina));
		bsdtar->return_value = 1;
		return (0);
	}

	rc = append_archive(bsdtar, a, ina, NULL);

	if (archive_errno(ina)) {
		bsdtar_warnc(bsdtar, 0, "Error reading archive %s: %s",
		    filename, archive_error_string(ina));
		bsdtar->return_value = 1;
	}
	archive_read_finish(ina);

	return (rc);
}

static int
append_archive_tarsnap(struct bsdtar *bsdtar, struct archive *a,
    const char *tapename)
{
	struct archive *ina;
	void * cookie;
	int rc;

	ina = archive_read_new();
	archive_read_support_format_tar(ina);
	archive_read_support_compression_none(ina);
	cookie = archive_read_open_multitape(ina, bsdtar->machinenum,
	    tapename);
	if (cookie == NULL) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(ina));
		bsdtar->return_value = 1;
		return (0);
	}

	rc = append_archive(bsdtar, a, ina, cookie);

	/* Handle errors which haven't already been reported. */
	if (archive_errno(ina)) {
		bsdtar_warnc(bsdtar, 0, "Error reading archive %s: %s",
		    tapename, archive_error_string(ina));
		bsdtar->return_value = 1;
	}
	archive_read_finish(ina);

	return (rc);
}

static int
append_archive(struct bsdtar *bsdtar, struct archive *a, struct archive *ina,
    void * cookie)
{
	struct archive_entry *in_entry;
	int e;

	while (0 == archive_read_next_header(ina, &in_entry)) {
		if (truncate_archive(bsdtar))
			break;
		if (checkpoint_archive(bsdtar, 0))
			exit(1);
		if (cookie == NULL)
			disk_pause(bsdtar);
		if (network_select(0))
			exit(1);

		if (!new_enough(bsdtar, archive_entry_pathname(in_entry),
			archive_entry_stat(in_entry)))
			continue;
		if (excluded(bsdtar, archive_entry_pathname(in_entry)))
			continue;
		if (bsdtar->option_interactive &&
		    !yes("copy '%s'", archive_entry_pathname(in_entry)))
			continue;
		if (bsdtar->verbose)
			safe_fprintf(stderr, "a %s",
			    archive_entry_pathname(in_entry));
		siginfo_setinfo(bsdtar, "copying",
		    archive_entry_pathname(in_entry),
		    archive_entry_size(in_entry));
		siginfo_printinfo(bsdtar, 0);

		if (MODE_HEADER(bsdtar, a))
			goto err_fatal;
		e = archive_write_header(a, in_entry);
		if (e != ARCHIVE_OK) {
			if (!bsdtar->verbose)
				bsdtar_warnc(bsdtar, 0, "%s: %s",
				    archive_entry_pathname(in_entry),
				    archive_error_string(a));
			else
				fprintf(stderr, ": %s", archive_error_string(a));
		}
		if (e == ARCHIVE_FATAL)
			exit(1);
		if (e < ARCHIVE_WARN)
			goto done;

		if (MODE_DATA(bsdtar, a))
			goto err_fatal;

		if (archive_entry_size(in_entry) == 0)
			archive_read_data_skip(ina);
		else if (cookie == NULL) {
			if (copy_file_data(bsdtar, a, ina))
				exit(1);
		} else {
			switch (archive_multitape_copy(ina, cookie, a,
			    bsdtar->write_cookie)) {
			case -1:
				goto err_fatal;
			case -2:
				goto err_read;
			}
		}

done:
		if (MODE_DONE(bsdtar, a))
			goto err_fatal;
		if (bsdtar->verbose)
			fprintf(stderr, "\n");
		continue;

err_read:
		bsdtar->return_value = 1;
		if (MODE_DONE(bsdtar, a))
			goto err_fatal;
		if (bsdtar->verbose)
			fprintf(stderr, "\n");
		break;

err_fatal:
		bsdtar_warnc(bsdtar, archive_errno(a), "%s",
		    archive_error_string(a));
		exit(1);
	}

	/* Note: If we got here, we saw no write errors, so return success. */
	return (0);
}

/* Helper function to copy data between archives. */
static int
copy_file_data(struct bsdtar *bsdtar, struct archive *a, struct archive *ina)
{
	ssize_t	bytes_read;
	ssize_t	bytes_written;
	off_t	progress = 0;

	bytes_read = archive_read_data(ina, bsdtar->buff, FILEDATABUFLEN);
	while (bytes_read > 0) {
		disk_pause(bsdtar);
		if (network_select(0))
			return (-1);

		siginfo_printinfo(bsdtar, progress);

		bytes_written = archive_write_data(a, bsdtar->buff,
		    bytes_read);
		if (bytes_written < bytes_read) {
			bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
			return (-1);
		}

		if (truncate_archive(bsdtar))
			break;
		if (checkpoint_archive(bsdtar, 1))
			return (-1);

		progress += bytes_written;
		bytes_read = archive_read_data(ina, bsdtar->buff,
		    FILEDATABUFLEN);
	}

	return (0);
}

/*
 * Add the file or dir hierarchy named by 'path' to the archive
 */
static void
write_hierarchy(struct bsdtar *bsdtar, struct archive *a, const char *path)
{
	struct archive_entry *entry = NULL, *spare_entry = NULL;
	struct tree *tree;
	char symlink_mode = bsdtar->symlink_mode;
	dev_t first_dev = 0;
	int dev_recorded = 0;
	int tree_ret;
	dev_t last_dev = 0;
	char * fstype;

	tree = tree_open(path);

	if (!tree) {
		bsdtar_warnc(bsdtar, errno, "%s: Cannot open", path);
		bsdtar->return_value = 1;
		return;
	}

	while ((tree_ret = tree_next(tree))) {
		int r;
		const char *name = tree_current_path(tree);
		const struct stat *st = NULL; /* info to use for this entry */
		const struct stat *lst = NULL; /* lstat() information */
		int descend;

		if (truncate_archive(bsdtar))
			break;
		if (checkpoint_archive(bsdtar, 0))
			exit(1);
		disk_pause(bsdtar);
		if (network_select(0))
			exit(1);

		if (tree_ret == TREE_ERROR_FATAL)
			bsdtar_errc(bsdtar, 1, tree_errno(tree),
			    "%s: Unable to continue traversing directory tree",
			    name);
		if (tree_ret == TREE_ERROR_DIR) {
			bsdtar_warnc(bsdtar, errno,
			    "%s: Couldn't visit directory", name);
			bsdtar->return_value = 1;
		}
		if (tree_ret != TREE_REGULAR)
			continue;

		/*
		 * If this file/dir is excluded by a filename
		 * pattern, skip it.
		 */
		if (excluded(bsdtar, name))
			continue;

		/*
		 * Get lstat() info from the tree library.
		 */
		lst = tree_current_lstat(tree);
		if (lst == NULL) {
			/* Couldn't lstat(); must not exist. */
			bsdtar_warnc(bsdtar, errno, "%s: Cannot stat", name);
			/* Return error if files disappear during traverse. */
			bsdtar->return_value = 1;
			continue;
		}

		/*
		 * Distinguish 'L'/'P'/'H' symlink following.
		 */
		switch(symlink_mode) {
		case 'H':
			/* 'H': After the first item, rest like 'P'. */
			symlink_mode = 'P';
			/* 'H': First item (from command line) like 'L'. */
			/* FALLTHROUGH */
		case 'L':
			/* 'L': Do descend through a symlink to dir. */
			descend = tree_current_is_dir(tree);
			/* 'L': Follow symlinks to files. */
			archive_read_disk_set_symlink_logical(bsdtar->diskreader);
			/* 'L': Archive symlinks as targets, if we can. */
			st = tree_current_stat(tree);
			if (st != NULL)
				break;
			/* If stat fails, we have a broken symlink;
			 * in that case, don't follow the link. */
			/* FALLTHROUGH */
		default:
			/* 'P': Don't descend through a symlink to dir. */
			descend = tree_current_is_physical_dir(tree);
			/* 'P': Don't follow symlinks to files. */
			archive_read_disk_set_symlink_physical(bsdtar->diskreader);
			/* 'P': Archive symlinks as symlinks. */
			st = lst;
			break;
		}

		if (bsdtar->option_no_subdirs)
			descend = 0;

		/*
		 * If user has asked us not to cross mount points,
		 * then don't descend into a dir on a different
		 * device.
		 */
		if (!dev_recorded) {
			last_dev = first_dev = lst->st_dev;
			dev_recorded = 1;
		}
		if (bsdtar->option_dont_traverse_mounts) {
			if (lst->st_dev != first_dev)
				descend = 0;
		}

		/*
		 * If the user did not specify --insane-filesystems, do not
		 * cross into a new filesystem which is known to be synthetic.
		 * Note that we will archive synthetic filesystems if we are
		 * explicitly told to do so.
		 */
		if ((bsdtar->option_insane_filesystems == 0) &&
		    (descend != 0) &&
		    (lst->st_dev != last_dev)) {
			fstype = getfstype(tree_current_access_path(tree));
			if (fstype == NULL)
				bsdtar_errc(bsdtar, 1, errno,
				    "%s: Error getting filesystem type",
				    name);
			if (getfstype_issynthetic(fstype)) {
				if (!bsdtar->option_quiet)
					bsdtar_warnc(bsdtar, 0,
					    "Not descending into filesystem of type %s: %s",
					    fstype, name);
				descend = 0;
			} else {
				/* This device is ok to archive. */
				last_dev = lst->st_dev;
			}
			free(fstype);
		}

		/*
		 * In -u mode, check that the file is newer than what's
		 * already in the archive; in all modes, obey --newerXXX flags.
		 */
		if (!new_enough(bsdtar, name, st)) {
			if (!descend)
				continue;
			if (bsdtar->option_interactive &&
			    !yes("add '%s'", name))
				continue;
			tree_descend(tree);
			continue;
		}

		archive_entry_free(entry);
		entry = archive_entry_new();

		archive_entry_set_pathname(entry, name);
		archive_entry_copy_sourcepath(entry,
		    tree_current_access_path(tree));

		/* Populate the archive_entry with metadata from the disk. */
		/* XXX TODO: Arrange to open a regular file before
		 * calling this so we can pass in an fd and shorten
		 * the race to query metadata.  The linkify dance
		 * makes this more complex than it might sound. */
		r = archive_read_disk_entry_from_file(bsdtar->diskreader,
		    entry, -1, st);
		if (r != ARCHIVE_OK)
			bsdtar_warnc(bsdtar, archive_errno(bsdtar->diskreader),
			    "%s", archive_error_string(bsdtar->diskreader));
		if (r < ARCHIVE_WARN)
			continue;

		/* XXX TODO: Just use flag data from entry; avoid the
		 * duplicate check here. */

		/*
		 * If this file/dir is flagged "nodump" and we're
		 * honoring such flags, skip this file/dir.
		 */
#if defined(HAVE_STRUCT_STAT_ST_FLAGS) && defined(UF_NODUMP)
		/* BSD systems store flags in struct stat */
		if (bsdtar->option_honor_nodump &&
		    (lst->st_flags & UF_NODUMP))
			continue;
#endif

#if defined(EXT2_NODUMP_FL)
		/* Linux uses ioctl to read flags. */
		if (bsdtar->option_honor_nodump) {
			unsigned long fflags, dummy;
			archive_entry_fflags(entry, &fflags, &dummy);
			if (fflags & EXT2_NODUMP_FL)
				continue;
		}
#endif

		/*
		 * Don't back up the cache directory or any files inside it.
		 */
		if ((lst->st_ino == bsdtar->cachedir_ino) &&
		    (lst->st_dev == bsdtar->cachedir_dev)) {
			if (!bsdtar->option_quiet)
				bsdtar_warnc(bsdtar, 0,
				    "Not adding cache directory to archive: %s",
				name);
			continue;
		}

		/*
		 * If the user vetoes this file/directory, skip it.
		 * We want this to be fairly late; if some other
		 * check would veto this file, we shouldn't bother
		 * the user with it.
		 */
		if (bsdtar->option_interactive &&
		    !yes("add '%s'", name))
			continue;

		/* Note: if user vetoes, we won't descend. */
		if (descend)
			tree_descend(tree);

		/*
		 * Rewrite the pathname to be archived.  If rewrite
		 * fails, skip the entry.
		 */
		if (edit_pathname(bsdtar, entry))
			continue;

		/*
		 * If this is a socket, skip the entry: POSIX requires that
		 * pax(1) emit a "diagnostic message" (i.e., warning) that
		 * sockets cannot be archived, but this can make backups of
		 * running systems very noisy.
		 */
		if (S_ISSOCK(st->st_mode))
			continue;

		/* Display entry as we process it.
		 * This format is required by SUSv2. */
		if (bsdtar->verbose)
			safe_fprintf(stderr, "a %s",
			    archive_entry_pathname(entry));

		/*
		 * If the user hasn't specifically asked to have the access
		 * time stored, zero it.  At the moment this usually only
		 * matters for files which have flags set, since the "posix
		 * restricted" format doesn't store access times for most
		 * other files.
		 */
		if (bsdtar->option_store_atime == 0)
			archive_entry_set_atime(entry, 0, 0);

		/* Non-regular files get archived with zero size. */
		if (!S_ISREG(st->st_mode))
			archive_entry_set_size(entry, 0);

		/* Record what we're doing, for SIGINFO / SIGUSR1. */
		siginfo_setinfo(bsdtar, "adding",
		    archive_entry_pathname(entry), archive_entry_size(entry));
		archive_entry_linkify(bsdtar->resolver, &entry, &spare_entry);

		/* Handle SIGINFO / SIGUSR1 request if one was made. */
		siginfo_printinfo(bsdtar, 0);

		while (entry != NULL) {
			write_entry_backend(bsdtar, a, entry, st,
			    tree_current_realpath(tree));
			archive_entry_free(entry);
			entry = spare_entry;
			spare_entry = NULL;
		}

		if (bsdtar->verbose)
			fprintf(stderr, "\n");
	}
	archive_entry_free(entry);
	if (tree_close(tree))
		bsdtar_errc(bsdtar, 1, 0, "Error traversing directory tree");
}

/*
 * Backend for write_entry.
 */
static void
write_entry_backend(struct bsdtar *bsdtar, struct archive *a,
    struct archive_entry *entry, const struct stat *st, const char *rpath)
{
	off_t			 skiplen;
	CCACHE_ENTRY		*cce = NULL;
	int			 filecached = 0;
	int fd = -1;
	int e;

	/*
	 * If this archive entry needs data, we have a canonical path to the
	 * relevant file, and the chunkification cache isn't disabled, ask
	 * the chunkification cache to find the entry for the file (if one
	 * already exists) and tell us if it can provide the entire file.
	 */
	if ((st != NULL) && S_ISREG(st->st_mode) && (rpath != NULL) &&
	    (archive_entry_size(entry) > 0) && (bsdtar->cachecrunch < 2) &&
	    (bsdtar->chunk_cache != NULL)) {
		cce = ccache_entry_lookup(bsdtar->chunk_cache, rpath, st,
		    bsdtar->write_cookie, &filecached);
	}

	/*
	 * Open the file if we need to write archive entry data and the
	 * chunkification cache can't provide all of it.
	 */
	if ((archive_entry_size(entry) > 0) && (filecached == 0)) {
		const char *pathname = archive_entry_sourcepath(entry);
		fd = open(pathname, O_RDONLY);
		if (fd == -1) {
			if (!bsdtar->verbose)
				bsdtar_warnc(bsdtar, errno,
				    "%s: could not open file", pathname);
			else
				fprintf(stderr, ": %s", strerror(errno));
			return;
		}
	}

	/* Write the archive header. */
	if (MODE_HEADER(bsdtar, a)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		exit(1);
	}
	e = archive_write_header(a, entry);
	if (e != ARCHIVE_OK) {
		if (!bsdtar->verbose)
			bsdtar_warnc(bsdtar, 0, "%s: %s",
			    archive_entry_pathname(entry),
			    archive_error_string(a));
		else
			fprintf(stderr, ": %s", archive_error_string(a));
	}

	if (e == ARCHIVE_FATAL)
		exit(1);

	/*
	 * If we opened a file earlier, write it out now.  Note that
	 * the format handler might have reset the size field to zero
	 * to inform us that the archive body won't get stored.  In
	 * that case, just skip the write.
	 */

	/* If the cache can provide the entire archive entry, do it. */
	if (e >= ARCHIVE_WARN && filecached &&
	    archive_entry_size(entry) > 0) {
		if (MODE_DATA(bsdtar, a)) {
			bsdtar_warnc(bsdtar, 0, "%s",
			    archive_error_string(a));
			exit(1);
		}
		skiplen = ccache_entry_write(cce, bsdtar->write_cookie);
		if (skiplen < st->st_size) {
			bsdtar_warnc(bsdtar, 0,
			    "Error writing cached archive entry");
			exit(1);
		}
		if (archive_write_skip(a, skiplen)) {
			bsdtar_warnc(bsdtar, 0, "%s",
			    archive_error_string(a));
			exit(1);
		}
	}

	/*
	 * We don't need to write anything now if the file was cached
	 * and the cache wrote it out earlier.
	 */
	if (e >= ARCHIVE_WARN && fd >= 0 && archive_entry_size(entry) > 0 &&
	    filecached == 0) {
		/* Switch into data mode. */
		if (MODE_DATA(bsdtar, a)) {
			bsdtar_warnc(bsdtar, 0, "%s",
			    archive_error_string(a));
			exit(1);
		}

		if (cce != NULL) {
			/* Ask the cache to write as much as possible. */
			skiplen = ccache_entry_writefile(cce,
			    bsdtar->write_cookie, bsdtar->cachecrunch, fd);
			if (skiplen < 0) {
				bsdtar_warnc(bsdtar, 0,
				    "Error writing archive");
				exit(1);
			}

			/* Skip forward in the file. */
			if (lseek(fd, skiplen, SEEK_SET) == -1) {
				bsdtar_warnc(bsdtar, errno, "lseek(%s)",
				    archive_entry_pathname(entry));
				exit(1);
			}

			/* Tell the archive layer that we've skipped. */
			if (archive_write_skip(a, skiplen)) {
				bsdtar_warnc(bsdtar, 0, "%s",
				    archive_error_string(a));
				exit(1);
			}
		}

		if (write_file_data(bsdtar, a, entry, fd))
			exit(1);
	}

	/* This entry is done. */
	if (!truncate_archive(bsdtar) && MODE_DONE(bsdtar, a)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		exit(1);
	}

	/* Tell the cache that we're done. */
	if (cce != NULL) {
		if (ccache_entry_end(bsdtar->chunk_cache, cce,
		    bsdtar->write_cookie, rpath, bsdtar->snaptime))
			exit(1);
		cce = NULL;
	}

	/*
	 * If we opened a file, close it now even if there was an error
	 * which made us decide not to write the archive body.
	 */
	if (fd >= 0)
		close(fd);
}

/* Helper function to copy file to archive. */
static int
write_file_data(struct bsdtar *bsdtar, struct archive *a,
    struct archive_entry *entry, int fd)
{
	ssize_t	bytes_read;
	ssize_t	bytes_written;
	off_t	progress = 0;

	bytes_read = read(fd, bsdtar->buff, FILEDATABUFLEN);
	while (bytes_read > 0) {
		disk_pause(bsdtar);
		if (network_select(0))
			return (-1);

		siginfo_printinfo(bsdtar, progress);

		bytes_written = archive_write_data(a, bsdtar->buff,
		    bytes_read);
		if (bytes_written < 0) {
			/* Write failed; this is bad */
			bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
			return (-1);
		}
		if (bytes_written < bytes_read) {
			/* Write was truncated; warn but continue. */
			if (!bsdtar->option_quiet)
				bsdtar_warnc(bsdtar, 0,
				    "%s: Truncated write; file may have "
				    "grown while being archived.",
				    archive_entry_pathname(entry));
			return (0);
		}

		if (truncate_archive(bsdtar))
			break;
		if (checkpoint_archive(bsdtar, 1))
			exit(1);

		progress += bytes_written;
		bytes_read = read(fd, bsdtar->buff, FILEDATABUFLEN);
	}
	return 0;
}

/*
 * Test if the specified file is new enough to include in the archive.
 */
int
new_enough(struct bsdtar *bsdtar, const char *path, const struct stat *st)
{

	(void)path;	/* UNUSED */

	/*
	 * If this file/dir is excluded by a time comparison, skip it.
	 */
	if (bsdtar->newer_ctime_sec > 0) {
		if (st->st_ctime < bsdtar->newer_ctime_sec)
			return (0); /* Too old, skip it. */
		if (st->st_ctime == bsdtar->newer_ctime_sec
		    && ARCHIVE_STAT_CTIME_NANOS(st)
		    <= bsdtar->newer_ctime_nsec)
			return (0); /* Too old, skip it. */
	}
	if (bsdtar->newer_mtime_sec > 0) {
		if (st->st_mtime < bsdtar->newer_mtime_sec)
			return (0); /* Too old, skip it. */
		if (st->st_mtime == bsdtar->newer_mtime_sec
		    && ARCHIVE_STAT_MTIME_NANOS(st)
		    <= bsdtar->newer_mtime_nsec)
			return (0); /* Too old, skip it. */
	}

	/* If the file wasn't rejected, include it. */
	return (1);
}
