/*-
 * Copyright 2006-2008 Colin Percival
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
__FBSDID("$FreeBSD: src/usr.bin/tar/write.c,v 1.65 2008/03/15 02:41:44 kientzle Exp $");

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
#ifdef HAVE_EXT2FS_EXT2_FS_H
#include <ext2fs/ext2_fs.h>
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
#ifdef HAVE_LINUX_EXT2_FS_H
#include <linux/ext2_fs.h>	/* for Linux file flags */
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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "bsdtar.h"
#include "tree.h"

#include "archive_multitape.h"
#include "ccache.h"
#include "network.h"
#include "sigquit.h"

/* Fixed size of uname/gname caches. */
#define	name_cache_size 101

static const char * const NO_NAME = "(noname)";

/* Is there a pending SIGINFO or SIGUSR1? */
static sig_atomic_t siginfo_received;

/* Initial size of link cache. */
#define	links_cache_initial_size 1024

struct links_cache {
	unsigned long		  number_entries;
	size_t			  number_buckets;
	struct links_entry	**buckets;
};

struct links_entry {
	struct links_entry	*next;
	struct links_entry	*previous;
	int			 links;
	dev_t			 dev;
	ino_t			 ino;
	char			*name;
};

struct name_cache {
	int	probes;
	int	hits;
	size_t	size;
	struct {
		id_t id;
		const char *name;
	} cache[name_cache_size];
};

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
static void		 create_cleanup(struct bsdtar *);
static void		 free_buckets(struct bsdtar *, struct links_cache *);
static void		 free_cache(struct name_cache *cache);
static int		 getdevino(struct archive *, const char *, dev_t *,
			     ino_t *);
static const char *	 lookup_gname(struct bsdtar *bsdtar, gid_t gid);
static int		 lookup_gname_helper(struct bsdtar *bsdtar,
			     const char **name, id_t gid);
static void		 lookup_hardlink(struct bsdtar *,
			     struct archive_entry *entry, const struct stat *);
static const char *	 lookup_uname(struct bsdtar *bsdtar, uid_t uid);
static int		 lookup_uname_helper(struct bsdtar *bsdtar,
			     const char **name, id_t uid);
static int		 new_enough(struct bsdtar *, const char *path,
			     const struct stat *);
static int		 print_info(void);
static void		 setup_acls(struct bsdtar *, struct archive_entry *,
			     const char *path);
static void		 setup_xattrs(struct bsdtar *, struct archive_entry *,
			     const char *path);
static void		 siginfo_handler(int sig);
static int		 truncate_archive(struct bsdtar *);
static void		 write_archive(struct archive *, struct bsdtar *);
static void		 write_entry(struct bsdtar *, struct archive *,
			     const struct stat *, const char *pathname,
			     const char *accpath, const char *rpath);
static int		 write_file_data(struct bsdtar *, struct archive *,
			     int fd);
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

	if (sigquit_received == 0)
		return (0);

	/* Tell the multitape code to truncate the archive. */
	archive_write_multitape_truncate(bsdtar->write_cookie);

	/* Yes, we need to stop writing now. */
	return (1);
}

/* Handler for SIGINFO / SIGUSR1. */
static void
siginfo_handler(int sig)
{

	(void)sig; /* UNUSED */

	/* Record that SIGINFO or SIGUSR1 has been received. */
	siginfo_received = 1;
}

/* Return and zero siginfo_received. */
static int
print_info(void)
{

	if (siginfo_received) {
		siginfo_received = 0;
		return (1);
	} else {
		return (0);
	}
}

void
tarsnap_mode_c(struct bsdtar *bsdtar)
{
	struct archive *a;
#ifdef SIGINFO
	void (*siginfo_old)(int);
#endif
	void (*sigusr1_old)(int);

	if (*bsdtar->argv == NULL && bsdtar->names_from_file == NULL)
		bsdtar_errc(bsdtar, 1, 0, "no files or directories specified");

	/* We want to catch SIGQUIT and ^Q. */
	if (sigquit_init())
		exit(1);

#ifdef SIGINFO
	/* We want to catch SIGINFO, if it exists. */
	siginfo_received = 0;
	siginfo_old = signal(SIGINFO, siginfo_handler);
#endif
	/* ... and treat SIGUSR1 the same way as SIGINFO. */
	sigusr1_old = signal(SIGUSR1, siginfo_handler);

	a = archive_write_new();

	/* We only support the pax restricted format. */
	archive_write_set_format_pax_restricted(a);

	/* Set the block size to zero -- we don't want buffering. */
	archive_write_set_bytes_per_block(a, 0);

	/* Open the archive, keeping a cookie for talking to the tape layer. */
	bsdtar->write_cookie = archive_write_open_multitape(a,
	    bsdtar->machinenum, bsdtar->cachedir, bsdtar->tapename,
	    bsdtar->argc_orig, bsdtar->argv_orig,
	    bsdtar->option_print_stats);
	if (bsdtar->write_cookie == NULL)
		bsdtar_errc(bsdtar, 1, 0, archive_error_string(a));

	/*
	 * Remember the device and inode numbers of the cache directory, so
	 * that we can skip is in write_hierarchy().
	 */
	if (getdevino(a, bsdtar->cachedir,
	    &bsdtar->cachedir_dev, &bsdtar->cachedir_ino))
		bsdtar_errc(bsdtar, 1, 0, archive_error_string(a));

	/* Read the chunkfication cache. */
	if (bsdtar->cachecrunch < 2) {
		bsdtar->chunk_cache = ccache_read(bsdtar->cachedir);
		if (bsdtar->chunk_cache == NULL)
			bsdtar_errc(bsdtar, 1, errno, "Error reading cache");
	}

	write_archive(a, bsdtar);

	if (bsdtar->option_totals && (bsdtar->return_value == 0)) {
		fprintf(stderr, "Total bytes written: " BSDTAR_FILESIZE_PRINTF "\n",
		    (BSDTAR_FILESIZE_TYPE)archive_position_compressed(a));
	}

	archive_write_finish(a);

#ifdef SIGINFO
	/* Restore old SIGINFO handler. */
	signal(SIGINFO, siginfo_old);
#endif
	/* And the old SIGUSR1 handler, too. */
	signal(SIGUSR1, sigusr1_old);

	/* Write the chunkification cache back to disk. */
	if (bsdtar->cachecrunch < 2) {
		if (ccache_write(bsdtar->chunk_cache, bsdtar->cachedir))
			bsdtar_errc(bsdtar, 1, errno, "Error writing cache");
	}

	/* Free the chunkification cache. */
	if (bsdtar->cachecrunch < 2)
		ccache_free(bsdtar->chunk_cache);
}

/*
 * Write user-specified files/dirs to opened archive.
 */
static void
write_archive(struct archive *a, struct bsdtar *bsdtar)
{
	const char *arg;

	if (bsdtar->names_from_file != NULL)
		archive_names_from_file(bsdtar, a);

	while (*bsdtar->argv) {
		if (truncate_archive(bsdtar))
			break;

		arg = *bsdtar->argv;
		if (arg[0] == '-' && arg[1] == 'C') {
			arg += 2;
			if (*arg == '\0') {
				bsdtar->argv++;
				arg = *bsdtar->argv;
				if (arg == NULL) {
					bsdtar_warnc(bsdtar, 1, 0,
					    "Missing argument for -C");
					bsdtar->return_value = 1;
					return;
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
				write_hierarchy(bsdtar, a, arg);
		}
		bsdtar->argv++;
	}

	create_cleanup(bsdtar);
	if (archive_write_close(a)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		bsdtar->return_value = 1;
	}
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
		set_chdir(bsdtar, line);
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
		if (print_info())
			fprintf(stderr, "copying %s\n",
			    archive_entry_pathname(in_entry));
		if (bsdtar->verbose)
			safe_fprintf(stderr, "a %s",
			    archive_entry_pathname(in_entry));

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
		if (cookie == NULL) {
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
	char	buff[64*1024];
	ssize_t	bytes_read;
	ssize_t	bytes_written;

	bytes_read = archive_read_data(ina, buff, sizeof(buff));
	while (bytes_read > 0) {
		if (network_select(0))
			return (-1);

		bytes_written = archive_write_data(a, buff, bytes_read);
		if (bytes_written < bytes_read) {
			bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
			return (-1);
		}

		if (truncate_archive(bsdtar))
			break;

		bytes_read = archive_read_data(ina, buff, sizeof(buff));
	}

	return (0);
}

/*
 * Add the file or dir hierarchy named by 'path' to the archive
 */
static void
write_hierarchy(struct bsdtar *bsdtar, struct archive *a, const char *path)
{
	struct tree *tree;
	char symlink_mode = bsdtar->symlink_mode;
	dev_t first_dev = 0;
	int dev_recorded = 0;
	int tree_ret;
#ifdef __linux
	int	 fd, r;
	unsigned long fflags;
#endif

	tree = tree_open(path);

	if (!tree) {
		bsdtar_warnc(bsdtar, errno, "%s: Cannot open", path);
		bsdtar->return_value = 1;
		return;
	}

	while ((tree_ret = tree_next(tree))) {
		const char *name = tree_current_path(tree);
		const struct stat *st = NULL, *lst = NULL;
		int descend;

		if (truncate_archive(bsdtar))
			break;
		if (network_select(0))
			exit(1);

		if (tree_ret == TREE_ERROR_DIR)
			bsdtar_warnc(bsdtar, errno, "%s: Couldn't visit directory", name);
		if (tree_ret != TREE_REGULAR)
			continue;
		lst = tree_current_lstat(tree);
		if (lst == NULL) {
			/* Couldn't lstat(); must not exist. */
			bsdtar_warnc(bsdtar, errno, "%s: Cannot stat", name);

			/*
			 * Report an error via the exit code if the failed
			 * path is a prefix of what the user provided via
			 * the command line.  (Testing for string equality
			 * here won't work due to trailing '/' characters.)
			 */
			if (memcmp(name, path, strlen(name)) == 0)
				bsdtar->return_value = 1;

			continue;
		}
		if (S_ISLNK(lst->st_mode))
			st = tree_current_stat(tree);
		/* Default: descend into any dir or symlink to dir. */
		/* We'll adjust this later on. */
		descend = 0;
		if ((st != NULL) && S_ISDIR(st->st_mode))
			descend = 1;
		if ((lst != NULL) && S_ISDIR(lst->st_mode))
			descend = 1;

		/*
		 * Don't back up the cache directory or any files inside it.
		 */
		if ((lst->st_ino == bsdtar->cachedir_ino) &&
		    (lst->st_dev == bsdtar->cachedir_dev)) {
			bsdtar_warnc(bsdtar, 0,
			    "Not adding cache directory to archive: %s", name);
			continue;
		}

		/*
		 * If user has asked us not to cross mount points,
		 * then don't descend into into a dir on a different
		 * device.
		 */
		if (!dev_recorded) {
			first_dev = lst->st_dev;
			dev_recorded = 1;
		}
		if (bsdtar->option_dont_traverse_mounts) {
			if (lst != NULL && lst->st_dev != first_dev)
				descend = 0;
		}

		/*
		 * If this file/dir is flagged "nodump" and we're
		 * honoring such flags, skip this file/dir.
		 */
#ifdef HAVE_CHFLAGS
		if (bsdtar->option_honor_nodump &&
		    (lst->st_flags & UF_NODUMP))
			continue;
#endif

#ifdef __linux
		/*
		 * Linux has a nodump flag too but to read it
		 * we have to open() the file/dir and do an ioctl on it...
		 */
		if (bsdtar->option_honor_nodump &&
		    ((fd = open(name, O_RDONLY|O_NONBLOCK)) >= 0) &&
		    ((r = ioctl(fd, EXT2_IOC_GETFLAGS, &fflags)),
			close(fd), r) >= 0 &&
		    (fflags & EXT2_NODUMP_FL))
			continue;
#endif

		/*
		 * If this file/dir is excluded by a filename
		 * pattern, skip it.
		 */
		if (excluded(bsdtar, name))
			continue;

		/*
		 * If the user vetoes this file/directory, skip it.
		 */
		if (bsdtar->option_interactive &&
		    !yes("add '%s'", name))
			continue;

		/*
		 * If this is a dir, decide whether or not to recurse.
		 */
		if (bsdtar->option_no_subdirs)
			descend = 0;

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
			/* 'L': Archive symlink to file as file. */
			lst = tree_current_stat(tree);
			/* If stat fails, we have a broken symlink;
			 * in that case, archive the link as such. */
			if (lst == NULL)
				lst = tree_current_lstat(tree);
			break;
		default:
			/* 'P': Don't descend through a symlink to dir. */
			if (!S_ISDIR(lst->st_mode))
				descend = 0;
			/* 'P': Archive symlink to file as symlink. */
			/* lst = tree_current_lstat(tree); */
			break;
		}

		if (descend)
			tree_descend(tree);

		/*
		 * Write the entry.  Note that write_entry() handles
		 * pathname editing and newness testing.
		 */
		write_entry(bsdtar, a, lst, name,
		    tree_current_access_path(tree),
		    tree_current_realpath(tree));
	}
	tree_close(tree);
}

/*
 * Add a single filesystem object to the archive.
 */
static void
write_entry(struct bsdtar *bsdtar, struct archive *a, const struct stat *st,
    const char *pathname, const char *accpath, const char *rpath)
{
	struct archive_entry	*entry;
	int			 e;
	int			 fd;
#ifdef __linux
	int			 r;
	unsigned long		 stflags;
#endif
	static char		 linkbuffer[PATH_MAX+1];
	CCACHE_ENTRY		*cce = NULL;
	int			 filecached = 0;
	off_t			 skiplen;

	fd = -1;
	entry = archive_entry_new();

	archive_entry_set_pathname(entry, pathname);

	/*
	 * Rewrite the pathname to be archived.  If rewrite
	 * fails, skip the entry.
	 */
	if (edit_pathname(bsdtar, entry))
		goto abort;

	/*
	 * In -u mode, check that the file is newer than what's
	 * already in the archive; in all modes, obey --newerXXX flags.
	 */
	if (!new_enough(bsdtar, archive_entry_pathname(entry), st))
		goto abort;

	/*
	 * If it's a socket, don't do anything with it: POSIX requires that
	 * pax(1) emit a "diagnostic message" (i.e., warning) that sockets
	 * cannot be archived, but this can make backups of running systems
	 * very noisy.
	 */
	if (S_ISSOCK(st->st_mode))
		goto abort;

	if (!S_ISDIR(st->st_mode) && (st->st_nlink > 1))
		lookup_hardlink(bsdtar, entry, st);

	/* Handle SIGINFO / SIGUSR1 request. */
	if (print_info())
		fprintf(stderr, "adding  %s\n",
		    archive_entry_pathname(entry));

	/* Display entry as we process it. This format is required by SUSv2. */
	if (bsdtar->verbose)
		safe_fprintf(stderr, "a %s", archive_entry_pathname(entry));

	/* Read symbolic link information. */
	if ((st->st_mode & S_IFMT) == S_IFLNK) {
		int lnklen;

		lnklen = readlink(accpath, linkbuffer, PATH_MAX);
		if (lnklen < 0) {
			if (!bsdtar->verbose)
				bsdtar_warnc(bsdtar, errno,
				    "%s: Couldn't read symbolic link",
				    pathname);
			else
				safe_fprintf(stderr,
				    ": Couldn't read symbolic link: %s",
				    strerror(errno));
			goto cleanup;
		}
		linkbuffer[lnklen] = 0;
		archive_entry_set_symlink(entry, linkbuffer);
	}

	/* Look up username and group name. */
	archive_entry_set_uname(entry, lookup_uname(bsdtar, st->st_uid));
	archive_entry_set_gname(entry, lookup_gname(bsdtar, st->st_gid));

#ifdef HAVE_CHFLAGS
	if (st->st_flags != 0)
		archive_entry_set_fflags(entry, st->st_flags, 0);
#endif

#ifdef __linux
	if ((S_ISREG(st->st_mode) || S_ISDIR(st->st_mode)) &&
	    ((fd = open(accpath, O_RDONLY|O_NONBLOCK)) >= 0) &&
	    ((r = ioctl(fd, EXT2_IOC_GETFLAGS, &stflags)), close(fd), (fd = -1), r) >= 0 &&
	    stflags) {
		archive_entry_set_fflags(entry, stflags, 0);
	}
#endif

	archive_entry_copy_stat(entry, st);
	setup_acls(bsdtar, entry, accpath);
	setup_xattrs(bsdtar, entry, accpath);

	/*
	 * If this is a regular file and we have a canonical path to it, ask
	 * the chunkification cache to find the entry for the file (if one
	 * already exists) and tell us if it can provide the entire file.
	 */
	if (S_ISREG(st->st_mode) && rpath != NULL &&
	    bsdtar->cachecrunch < 2) {
		cce = ccache_entry_lookup(bsdtar->chunk_cache, rpath, st,
		    bsdtar->write_cookie, &filecached);
	}

	/*
	 * If it's a regular file (and non-zero in size) make sure we
	 * can open it before we start to write.  In particular, note
	 * that we can always archive a zero-length file, even if we
	 * can't read it.
	 */
	/*
	 * We don't need to open the file if the chunkification cache can
	 * provide all of its contents.
	 */
	if (S_ISREG(st->st_mode) && st->st_size > 0 && filecached == 0) {
		fd = open(accpath, O_RDONLY);
		if (fd < 0) {
			if (!bsdtar->verbose)
				bsdtar_warnc(bsdtar, errno,
				    "%s: could not open file", pathname);
			else
				fprintf(stderr, ": %s", strerror(errno));
			goto cleanup;
		}
	}

	/*
	 * If the user hasn't specifically asked to have the access time
	 * stored, zero it.  At the moment this usually only matters for
	 * files which have flags set, since the "posix restricted" format
	 * doesn't store access times for most other files.
	 */
	if (bsdtar->option_store_atime == 0)
		archive_entry_set_atime(entry, 0, 0);

	/* Non-regular files get archived with zero size. */
	if (!S_ISREG(st->st_mode))
		archive_entry_set_size(entry, 0);

	/* Write the archive header. */
	if (MODE_HEADER(bsdtar, a)) {
		bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
		exit(1);
	}
	e = archive_write_header(a, entry);
	if (e != ARCHIVE_OK) {
		if (!bsdtar->verbose)
			bsdtar_warnc(bsdtar, 0, "%s: %s", pathname,
			    archive_error_string(a));
		else
			fprintf(stderr, ": %s", archive_error_string(a));
	}

	if (e == ARCHIVE_FATAL)
		exit(1);

	/* If the cache can provide the entire archive entry, do it. */
	if (e >= ARCHIVE_WARN && filecached) {
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
	 * If we opened a file earlier, write it out now.  Note that
	 * the format handler might have reset the size field to zero
	 * to inform us that the archive body won't get stored.  In
	 * that case, just skip the write.
	 */
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
				    pathname);
				exit(1);
			}

			/* Tell the archive layer that we've skipped. */
			if (archive_write_skip(a, skiplen)) {
				bsdtar_warnc(bsdtar, 0, "%s",
				    archive_error_string(a));
				exit(1);
			}
		}

		if (write_file_data(bsdtar, a, fd))
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

cleanup:
	ccache_entry_free(cce, bsdtar->write_cookie);

	if (bsdtar->verbose)
		fprintf(stderr, "\n");

abort:
	if (fd >= 0)
		close(fd);

	if (entry != NULL)
		archive_entry_free(entry);
}


/* Helper function to copy file to archive, with stack-allocated buffer. */
static int
write_file_data(struct bsdtar *bsdtar, struct archive *a, int fd)
{
	char	buff[64*1024];
	ssize_t	bytes_read;
	ssize_t	bytes_written;

	/* XXX TODO: Allocate buffer on heap and store pointer to
	 * it in bsdtar structure; arrange cleanup as well. XXX */

	bytes_read = read(fd, buff, sizeof(buff));
	while (bytes_read > 0) {
		if (network_select(0))
			return (-1);

		bytes_written = archive_write_data(a, buff, bytes_read);
		if (bytes_written < 0) {
			/* Write failed; this is bad */
			bsdtar_warnc(bsdtar, 0, "%s", archive_error_string(a));
			return (-1);
		}
		if (bytes_written < bytes_read) {
			/* Write was truncated; warn but continue. */
			bsdtar_warnc(bsdtar, 0,
			    "Truncated write; file may have grown while being archived.");
			return (0);
		}

		if (truncate_archive(bsdtar))
			break;

		bytes_read = read(fd, buff, sizeof(buff));
	}
	return 0;
}


static void
create_cleanup(struct bsdtar *bsdtar)
{
	/* Free inode->pathname map used for hardlink detection. */
	if (bsdtar->links_cache != NULL) {
		free_buckets(bsdtar, bsdtar->links_cache);
		free(bsdtar->links_cache);
		bsdtar->links_cache = NULL;
	}

	free_cache(bsdtar->uname_cache);
	bsdtar->uname_cache = NULL;
	free_cache(bsdtar->gname_cache);
	bsdtar->gname_cache = NULL;
}


static void
free_buckets(struct bsdtar *bsdtar, struct links_cache *links_cache)
{
	size_t i;

	if (links_cache->buckets == NULL)
		return;

	for (i = 0; i < links_cache->number_buckets; i++) {
		while (links_cache->buckets[i] != NULL) {
			struct links_entry *lp = links_cache->buckets[i]->next;
			if (bsdtar->option_warn_links)
				bsdtar_warnc(bsdtar, 0, "Missing links to %s",
				    links_cache->buckets[i]->name);
			if (links_cache->buckets[i]->name != NULL)
				free(links_cache->buckets[i]->name);
			free(links_cache->buckets[i]);
			links_cache->buckets[i] = lp;
		}
	}
	free(links_cache->buckets);
	links_cache->buckets = NULL;
}

static void
lookup_hardlink(struct bsdtar *bsdtar, struct archive_entry *entry,
    const struct stat *st)
{
	struct links_cache	*links_cache;
	struct links_entry	*le, **new_buckets;
	int			 hash;
	size_t			 i, new_size;

	/* If necessary, initialize the links cache. */
	links_cache = bsdtar->links_cache;
	if (links_cache == NULL) {
		bsdtar->links_cache = malloc(sizeof(struct links_cache));
		if (bsdtar->links_cache == NULL)
			bsdtar_errc(bsdtar, 1, ENOMEM,
			    "No memory for hardlink detection.");
		links_cache = bsdtar->links_cache;
		memset(links_cache, 0, sizeof(struct links_cache));
		links_cache->number_buckets = links_cache_initial_size;
		links_cache->buckets = malloc(links_cache->number_buckets *
		    sizeof(links_cache->buckets[0]));
		if (links_cache->buckets == NULL) {
			bsdtar_errc(bsdtar, 1, ENOMEM,
			    "No memory for hardlink detection.");
		}
		for (i = 0; i < links_cache->number_buckets; i++)
			links_cache->buckets[i] = NULL;
	}

	/* If the links cache overflowed and got flushed, don't bother. */
	if (links_cache->buckets == NULL)
		return;

	/* If the links cache is getting too full, enlarge the hash table. */
	if (links_cache->number_entries > links_cache->number_buckets * 2)
	{
		new_size = links_cache->number_buckets * 2;
		new_buckets = malloc(new_size * sizeof(struct links_entry *));

		if (new_buckets != NULL) {
			memset(new_buckets, 0,
			    new_size * sizeof(struct links_entry *));
			for (i = 0; i < links_cache->number_buckets; i++) {
				while (links_cache->buckets[i] != NULL) {
					/* Remove entry from old bucket. */
					le = links_cache->buckets[i];
					links_cache->buckets[i] = le->next;

					/* Add entry to new bucket. */
					hash = (le->dev ^ le->ino) % new_size;

					if (new_buckets[hash] != NULL)
						new_buckets[hash]->previous =
						    le;
					le->next = new_buckets[hash];
					le->previous = NULL;
					new_buckets[hash] = le;
				}
			}
			free(links_cache->buckets);
			links_cache->buckets = new_buckets;
			links_cache->number_buckets = new_size;
		} else {
			free_buckets(bsdtar, links_cache);
			bsdtar_warnc(bsdtar, ENOMEM,
			    "No more memory for recording hard links");
			bsdtar_warnc(bsdtar, 0,
			    "Remaining links will be dumped as full files");
		}
	}

	/* Try to locate this entry in the links cache. */
	hash = ( st->st_dev ^ st->st_ino ) % links_cache->number_buckets;
	for (le = links_cache->buckets[hash]; le != NULL; le = le->next) {
		if (le->dev == st->st_dev && le->ino == st->st_ino) {
			archive_entry_copy_hardlink(entry, le->name);

			/*
			 * Decrement link count each time and release
			 * the entry if it hits zero.  This saves
			 * memory and is necessary for proper -l
			 * implementation.
			 */
			if (--le->links <= 0) {
				if (le->previous != NULL)
					le->previous->next = le->next;
				if (le->next != NULL)
					le->next->previous = le->previous;
				if (le->name != NULL)
					free(le->name);
				if (links_cache->buckets[hash] == le)
					links_cache->buckets[hash] = le->next;
				links_cache->number_entries--;
				free(le);
			}

			return;
		}
	}

	/* Add this entry to the links cache. */
	le = malloc(sizeof(struct links_entry));
	if (le != NULL)
		le->name = strdup(archive_entry_pathname(entry));
	if ((le == NULL) || (le->name == NULL)) {
		free_buckets(bsdtar, links_cache);
		bsdtar_warnc(bsdtar, ENOMEM,
		    "No more memory for recording hard links");
		bsdtar_warnc(bsdtar, 0,
		    "Remaining hard links will be dumped as full files");
		if (le != NULL)
			free(le);
		return;
	}
	if (links_cache->buckets[hash] != NULL)
		links_cache->buckets[hash]->previous = le;
	links_cache->number_entries++;
	le->next = links_cache->buckets[hash];
	le->previous = NULL;
	links_cache->buckets[hash] = le;
	le->dev = st->st_dev;
	le->ino = st->st_ino;
	le->links = st->st_nlink - 1;
}

#ifdef HAVE_POSIX_ACL
static void		setup_acl(struct bsdtar *bsdtar,
			     struct archive_entry *entry, const char *accpath,
			     int acl_type, int archive_entry_acl_type);

static void
setup_acls(struct bsdtar *bsdtar, struct archive_entry *entry,
    const char *accpath)
{
	archive_entry_acl_clear(entry);

	setup_acl(bsdtar, entry, accpath,
	    ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_TYPE_ACCESS);
	/* Only directories can have default ACLs. */
	if (S_ISDIR(archive_entry_mode(entry)))
		setup_acl(bsdtar, entry, accpath,
		    ACL_TYPE_DEFAULT, ARCHIVE_ENTRY_ACL_TYPE_DEFAULT);
}

static void
setup_acl(struct bsdtar *bsdtar, struct archive_entry *entry,
    const char *accpath, int acl_type, int archive_entry_acl_type)
{
	acl_t		 acl;
	acl_tag_t	 acl_tag;
	acl_entry_t	 acl_entry;
	acl_permset_t	 acl_permset;
	int		 s, ae_id, ae_tag, ae_perm;
	const char	*ae_name;

	/* Retrieve access ACL from file. */
	acl = acl_get_file(accpath, acl_type);
	if (acl != NULL) {
		s = acl_get_entry(acl, ACL_FIRST_ENTRY, &acl_entry);
		while (s == 1) {
			ae_id = -1;
			ae_name = NULL;

			acl_get_tag_type(acl_entry, &acl_tag);
			if (acl_tag == ACL_USER) {
				ae_id = (int)*(uid_t *)acl_get_qualifier(acl_entry);
				ae_name = lookup_uname(bsdtar, ae_id);
				ae_tag = ARCHIVE_ENTRY_ACL_USER;
			} else if (acl_tag == ACL_GROUP) {
				ae_id = (int)*(gid_t *)acl_get_qualifier(acl_entry);
				ae_name = lookup_gname(bsdtar, ae_id);
				ae_tag = ARCHIVE_ENTRY_ACL_GROUP;
			} else if (acl_tag == ACL_MASK) {
				ae_tag = ARCHIVE_ENTRY_ACL_MASK;
			} else if (acl_tag == ACL_USER_OBJ) {
				ae_tag = ARCHIVE_ENTRY_ACL_USER_OBJ;
			} else if (acl_tag == ACL_GROUP_OBJ) {
				ae_tag = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
			} else if (acl_tag == ACL_OTHER) {
				ae_tag = ARCHIVE_ENTRY_ACL_OTHER;
			} else {
				/* Skip types that libarchive can't support. */
				continue;
			}

			acl_get_permset(acl_entry, &acl_permset);
			ae_perm = 0;
			/*
			 * acl_get_perm() is spelled differently on different
			 * platforms; see bsdtar_platform.h for details.
			 */
			if (ACL_GET_PERM(acl_permset, ACL_EXECUTE))
				ae_perm |= ARCHIVE_ENTRY_ACL_EXECUTE;
			if (ACL_GET_PERM(acl_permset, ACL_READ))
				ae_perm |= ARCHIVE_ENTRY_ACL_READ;
			if (ACL_GET_PERM(acl_permset, ACL_WRITE))
				ae_perm |= ARCHIVE_ENTRY_ACL_WRITE;

			archive_entry_acl_add_entry(entry,
			    archive_entry_acl_type, ae_perm, ae_tag,
			    ae_id, ae_name);

			s = acl_get_entry(acl, ACL_NEXT_ENTRY, &acl_entry);
		}
		acl_free(acl);
	}
}
#else
static void
setup_acls(struct bsdtar *bsdtar, struct archive_entry *entry,
    const char *accpath)
{
	(void)bsdtar;
	(void)entry;
	(void)accpath;
}
#endif

#if HAVE_LISTXATTR && HAVE_LLISTXATTR && HAVE_GETXATTR && HAVE_LGETXATTR

static void
setup_xattr(struct bsdtar *bsdtar, struct archive_entry *entry,
    const char *accpath, const char *name)
{
	size_t size;
	void *value = NULL;
	char symlink_mode = bsdtar->symlink_mode;

	if (symlink_mode == 'H')
		size = getxattr(accpath, name, NULL, 0);
	else
		size = lgetxattr(accpath, name, NULL, 0);

	if (size == -1) {
		bsdtar_warnc(bsdtar, errno, "Couldn't get extended attribute");
		return;
	}

	if (size > 0 && (value = malloc(size)) == NULL) {
		bsdtar_errc(bsdtar, 1, errno, "Out of memory");
		return;
	}

	if (symlink_mode == 'H')
		size = getxattr(accpath, name, value, size);
	else
		size = lgetxattr(accpath, name, value, size);

	if (size == -1) {
		bsdtar_warnc(bsdtar, errno, "Couldn't get extended attribute");
		return;
	}

	archive_entry_xattr_add_entry(entry, name, value, size);

	free(value);
}

/*
 * Linux extended attribute support
 */
static void
setup_xattrs(struct bsdtar *bsdtar, struct archive_entry *entry,
    const char *accpath)
{
	char *list, *p;
	size_t list_size;
	char symlink_mode = bsdtar->symlink_mode;

	if (symlink_mode == 'H')
		list_size = listxattr(accpath, NULL, 0);
	else
		list_size = llistxattr(accpath, NULL, 0);

	if (list_size == -1) {
		bsdtar_warnc(bsdtar, errno,
			"Couldn't list extended attributes");
		return;
	} else if (list_size == 0)
		return;

	if ((list = malloc(list_size)) == NULL) {
		bsdtar_errc(bsdtar, 1, errno, "Out of memory");
		return;
	}

	if (symlink_mode == 'H')
		list_size = listxattr(accpath, list, list_size);
	else
		list_size = llistxattr(accpath, list, list_size);

	if (list_size == -1) {
		bsdtar_warnc(bsdtar, errno,
			"Couldn't list extended attributes");
		free(list);
		return;
	}

	for (p = list; (p - list) < list_size; p += strlen(p) + 1) {
		if (strncmp(p, "system.", 7) == 0 ||
				strncmp(p, "xfsroot.", 8) == 0)
			continue;

		setup_xattr(bsdtar, entry, accpath, p);
	}

	free(list);
}

#else

/*
 * Generic (stub) extended attribute support.
 */
static void
setup_xattrs(struct bsdtar *bsdtar, struct archive_entry *entry,
    const char *accpath)
{
	(void)bsdtar; /* UNUSED */
	(void)entry; /* UNUSED */
	(void)accpath; /* UNUSED */
}

#endif

static void
free_cache(struct name_cache *cache)
{
	size_t i;

	if (cache != NULL) {
		for (i = 0; i < cache->size; i++) {
			if (cache->cache[i].name != NULL &&
			    cache->cache[i].name != NO_NAME)
				free((void *)(uintptr_t)cache->cache[i].name);
		}
		free(cache);
	}
}

/*
 * Lookup uid/gid from uname/gname, return NULL if no match.
 */
static const char *
lookup_name(struct bsdtar *bsdtar, struct name_cache **name_cache_variable,
    int (*lookup_fn)(struct bsdtar *, const char **, id_t), id_t id)
{
	struct name_cache	*cache;
	const char *name;
	int slot;


	if (*name_cache_variable == NULL) {
		*name_cache_variable = malloc(sizeof(struct name_cache));
		if (*name_cache_variable == NULL)
			bsdtar_errc(bsdtar, 1, ENOMEM, "No more memory");
		memset(*name_cache_variable, 0, sizeof(struct name_cache));
		(*name_cache_variable)->size = name_cache_size;
	}

	cache = *name_cache_variable;
	cache->probes++;

	slot = id % cache->size;
	if (cache->cache[slot].name != NULL) {
		if (cache->cache[slot].id == id) {
			cache->hits++;
			if (cache->cache[slot].name == NO_NAME)
				return (NULL);
			return (cache->cache[slot].name);
		}
		if (cache->cache[slot].name != NO_NAME)
			free((void *)(uintptr_t)cache->cache[slot].name);
		cache->cache[slot].name = NULL;
	}

	if (lookup_fn(bsdtar, &name, id) == 0) {
		if (name == NULL || name[0] == '\0') {
			/* Cache the negative response. */
			cache->cache[slot].name = NO_NAME;
			cache->cache[slot].id = id;
		} else {
			cache->cache[slot].name = strdup(name);
			if (cache->cache[slot].name != NULL) {
				cache->cache[slot].id = id;
				return (cache->cache[slot].name);
			}
			/*
			 * Conveniently, NULL marks an empty slot, so
			 * if the strdup() fails, we've just failed to
			 * cache it.  No recovery necessary.
			 */
		}
	}
	return (NULL);
}

static const char *
lookup_uname(struct bsdtar *bsdtar, uid_t uid)
{
	return (lookup_name(bsdtar, &bsdtar->uname_cache,
		    &lookup_uname_helper, (id_t)uid));
}

static int
lookup_uname_helper(struct bsdtar *bsdtar, const char **name, id_t id)
{
	struct passwd	*pwent;

	(void)bsdtar; /* UNUSED */

	errno = 0;
	pwent = getpwuid((uid_t)id);
	if (pwent == NULL) {
		*name = NULL;
		if (errno != 0)
			bsdtar_warnc(bsdtar, errno, "getpwuid(%d) failed", id);
		return (errno);
	}

	*name = pwent->pw_name;
	return (0);
}

static const char *
lookup_gname(struct bsdtar *bsdtar, gid_t gid)
{
	return (lookup_name(bsdtar, &bsdtar->gname_cache,
		    &lookup_gname_helper, (id_t)gid));
}

static int
lookup_gname_helper(struct bsdtar *bsdtar, const char **name, id_t id)
{
	struct group	*grent;

	(void)bsdtar; /* UNUSED */

	errno = 0;
	grent = getgrgid((gid_t)id);
	if (grent == NULL) {
		*name = NULL;
		if (errno != 0)
			bsdtar_warnc(bsdtar, errno, "getgrgid(%d) failed", id);
		return (errno);
	}

	*name = grent->gr_name;
	return (0);
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
