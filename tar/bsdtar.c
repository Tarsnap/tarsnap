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
__FBSDID("$FreeBSD: src/usr.bin/tar/bsdtar.c,v 1.90 2008/05/19 18:38:01 cperciva Exp $");

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#else
struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};
#define	no_argument 0
#define	required_argument 1
#endif
#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ZLIB_H
#include <zlib.h>
#endif

#include "bsdtar.h"
#include "crypto.h"
#include "network.h"
#include "storage.h"
#include "sysendian.h"

#if !HAVE_DECL_OPTARG
extern int optarg;
#endif

#if !HAVE_DECL_OPTIND
extern int optind;
#endif

/* External function to parse a date/time string (from getdate.y) */
time_t get_date(const char *);

static int		 bsdtar_getopt(struct bsdtar *, const char *optstring,
    const struct option **poption);
static void		 build_dir(struct bsdtar *, const char *dir,
			     const char *diropt);
static void		 configfile(struct bsdtar *, const char *fname);
static int		 configfile_helper(struct bsdtar *bsdtar,
			     const char *line);
static void		 load_keys(struct bsdtar *, const char *path);
static void		 long_help(struct bsdtar *);
static void		 only_mode(struct bsdtar *, const char *opt,
			     const char *valid);
static void		 set_mode(struct bsdtar *, int opt, const char *optstr);
static void		 version(void);

/*
 * The leading '+' here forces the GNU version of getopt() (as well as
 * both the GNU and BSD versions of getopt_long) to stop at the first
 * non-option.  Otherwise, GNU getopt() permutes the arguments and
 * screws up -C processing.
 */
static const char *tar_opts = "+@:BC:cdf:HhI:kLlmnOoPprtT:UvW:wX:x";

/*
 * Most of these long options are deliberately not documented.  They
 * are provided only to make life easier for people who also use GNU tar.
 * The only long options documented in the manual page are the ones
 * with no corresponding short option, such as --exclude, --nodump,
 * and --fast-read.
 *
 * On systems that lack getopt_long, long options can be specified
 * using -W longopt and -W longopt=value, e.g. "-W nodump" is the same
 * as "--nodump" and "-W exclude=pattern" is the same as "--exclude
 * pattern".  This does not rely the GNU getopt() "W;" extension, so
 * should work correctly on any system with a POSIX-compliant getopt().
 */

/* Fake short equivalents for long options that otherwise lack them. */
enum {
	OPTION_AGGRESSIVE_NETWORKING=1,
	OPTION_CACHEDIR,
	OPTION_CHECK_LINKS,
	OPTION_CHROOT,
	OPTION_EXCLUDE,
	OPTION_FSCK,
	OPTION_HELP,
	OPTION_INCLUDE,
	OPTION_KEYFILE,
	OPTION_KEEP_NEWER_FILES,
	OPTION_LIST_ARCHIVES,
	OPTION_LOWMEM,
	OPTION_NEWER_CTIME,
	OPTION_NEWER_CTIME_THAN,
	OPTION_NEWER_MTIME,
	OPTION_NEWER_MTIME_THAN,
	OPTION_NODUMP,
	OPTION_NO_SAME_OWNER,
	OPTION_NO_SAME_PERMISSIONS,
	OPTION_NULL,
	OPTION_ONE_FILE_SYSTEM,
	OPTION_PRINT_STATS,
	OPTION_SNAPTIME,
	OPTION_STORE_ATIME,
	OPTION_STRIP_COMPONENTS,
	OPTION_TOTALS,
	OPTION_VERSION,
	OPTION_VERYLOWMEM
};

/*
 * If you add anything, be very careful to keep this list properly
 * sorted, as the -W logic relies on it.
 */
static const struct option tar_longopts[] = {
	{ "absolute-paths",     no_argument,       NULL, 'P' },
	{ "aggressive-networking", no_argument,	   NULL, OPTION_AGGRESSIVE_NETWORKING },
	{ "cachedir",		required_argument, NULL, OPTION_CACHEDIR },
	{ "cd",                 required_argument, NULL, 'C' },
	{ "check-links",        no_argument,       NULL, OPTION_CHECK_LINKS },
	{ "chroot",             no_argument,       NULL, OPTION_CHROOT },
	{ "confirmation",       no_argument,       NULL, 'w' },
	{ "create",             no_argument,       NULL, 'c' },
	{ "dereference",	no_argument,	   NULL, 'L' },
	{ "directory",          required_argument, NULL, 'C' },
	{ "exclude",            required_argument, NULL, OPTION_EXCLUDE },
	{ "exclude-from",       required_argument, NULL, 'X' },
	{ "extract",            no_argument,       NULL, 'x' },
	{ "fast-read",          no_argument,       NULL, 'q' },
	{ "file",               required_argument, NULL, 'f' },
	{ "files-from",         required_argument, NULL, 'T' },
	{ "fsck",		no_argument,	   NULL, OPTION_FSCK },
	{ "help",               no_argument,       NULL, OPTION_HELP },
	{ "include",            required_argument, NULL, OPTION_INCLUDE },
	{ "interactive",        no_argument,       NULL, 'w' },
	{ "insecure",           no_argument,       NULL, 'P' },
	{ "keep-newer-files",   no_argument,       NULL, OPTION_KEEP_NEWER_FILES },
	{ "keep-old-files",     no_argument,       NULL, 'k' },
	{ "keyfile",		required_argument, NULL, OPTION_KEYFILE },
	{ "list",               no_argument,       NULL, 't' },
	{ "list-archives",	no_argument,	   NULL, OPTION_LIST_ARCHIVES },
	{ "lowmem",		no_argument,	   NULL, OPTION_LOWMEM },
	{ "modification-time",  no_argument,       NULL, 'm' },
	{ "newer",		required_argument, NULL, OPTION_NEWER_CTIME },
	{ "newer-ctime",	required_argument, NULL, OPTION_NEWER_CTIME },
	{ "newer-ctime-than",	required_argument, NULL, OPTION_NEWER_CTIME_THAN },
	{ "newer-mtime",	required_argument, NULL, OPTION_NEWER_MTIME },
	{ "newer-mtime-than",	required_argument, NULL, OPTION_NEWER_MTIME_THAN },
	{ "newer-than",		required_argument, NULL, OPTION_NEWER_CTIME_THAN },
	{ "nodump",             no_argument,       NULL, OPTION_NODUMP },
	{ "norecurse",          no_argument,       NULL, 'n' },
	{ "no-recursion",       no_argument,       NULL, 'n' },
	{ "no-same-owner",	no_argument,	   NULL, OPTION_NO_SAME_OWNER },
	{ "no-same-permissions",no_argument,	   NULL, OPTION_NO_SAME_PERMISSIONS },
	{ "null",		no_argument,	   NULL, OPTION_NULL },
	{ "one-file-system",	no_argument,	   NULL, OPTION_ONE_FILE_SYSTEM },
	{ "preserve-permissions", no_argument,     NULL, 'p' },
	{ "print-stats",	no_argument,	   NULL, OPTION_PRINT_STATS },
	{ "read-full-blocks",	no_argument,	   NULL, 'B' },
	{ "same-permissions",   no_argument,       NULL, 'p' },
	{ "snaptime",		required_argument, NULL, OPTION_SNAPTIME },
	{ "store-atime",	no_argument,	   NULL, OPTION_STORE_ATIME },
	{ "strip-components",	required_argument, NULL, OPTION_STRIP_COMPONENTS },
	{ "to-stdout",          no_argument,       NULL, 'O' },
	{ "totals",		no_argument,       NULL, OPTION_TOTALS },
	{ "unlink",		no_argument,       NULL, 'U' },
	{ "unlink-first",	no_argument,       NULL, 'U' },
	{ "verbose",            no_argument,       NULL, 'v' },
	{ "version",            no_argument,       NULL, OPTION_VERSION },
	{ "verylowmem",		no_argument,	   NULL, OPTION_VERYLOWMEM },
	{ NULL, 0, NULL, 0 }
};

/* A basic set of security flags to request from libarchive. */
#define	SECURITY					\
	(ARCHIVE_EXTRACT_SECURE_SYMLINKS		\
	 | ARCHIVE_EXTRACT_SECURE_NODOTDOT)

int
main(int argc, char **argv)
{
	struct bsdtar		*bsdtar, bsdtar_storage;
	const struct option	*option;
	int			 opt;
	char			 possible_help_request;
	char			 buff[16];
	char			 cachedir[PATH_MAX + 1];
	char			*homedir;
	char			*conffile;
	const char		*missingkey;

	/*
	 * Use a pointer for consistency, but stack-allocated storage
	 * for ease of cleanup.
	 */
	bsdtar = &bsdtar_storage;
	memset(bsdtar, 0, sizeof(*bsdtar));

	/* Need bsdtar->progname before calling bsdtar_warnc. */
	if (*argv == NULL)
		bsdtar->progname = "tarsnap";
	else {
		bsdtar->progname = strrchr(*argv, '/');
		if (bsdtar->progname != NULL)
			bsdtar->progname++;
		else
			bsdtar->progname = *argv;
	}

	if (setlocale(LC_ALL, "") == NULL)
		bsdtar_warnc(bsdtar, 0, "Failed to set default locale");
#if defined(HAVE_NL_LANGINFO) && defined(HAVE_D_MD_ORDER)
	bsdtar->day_first = (*nl_langinfo(D_MD_ORDER) == 'd');
#endif
	possible_help_request = 0;

	/* Initialize entropy subsystem. */
	if (crypto_entropy_init())
		exit(1);

	/* Initialize key cache.  We don't have any keys yet. */
	if (crypto_keys_init())
		exit(1);

	/* Initialize network layer. */
	if (network_init())
		exit(1);

	/*
	 * Unless specified otherwise, we consider ourselves to be
	 * constructing a snapshot of the disk as it is right now.
	 */
	/*
	 * POSIX doesn't provide any mechanism for distinguishing between
	 * an error and the time (time_t)(-1).  Since we only use this to
	 * avoid race conditions in the chunkification cache (i.e., so
	 * that we can determine if a file has been modified since it was
	 * last backed up), and hopefully nobody will have any files with
	 * negative last-modified dates, an error return of (-1) can be
	 * handled the same was as a legitimate return of (-1): Nothing
	 * gets cached.
	 */
	bsdtar->snaptime = time(NULL);

	/* Store original argument vector. */
	bsdtar->argc_orig = argc;
	bsdtar->argv_orig = argv;

	/* Look up uid of current user for future reference */
	bsdtar->user_uid = geteuid();

	/* Default: preserve mod time on extract */
	bsdtar->extract_flags = ARCHIVE_EXTRACT_TIME;

	/* Default: Perform basic security checks. */
	bsdtar->extract_flags |= SECURITY;

	/* Defaults for root user: */
	if (bsdtar->user_uid == 0) {
		/* --same-owner */
		bsdtar->extract_flags |= ARCHIVE_EXTRACT_OWNER;
		/* -p */
		bsdtar->extract_flags |= ARCHIVE_EXTRACT_PERM;
		bsdtar->extract_flags |= ARCHIVE_EXTRACT_ACL;
		bsdtar->extract_flags |= ARCHIVE_EXTRACT_XATTR;
		bsdtar->extract_flags |= ARCHIVE_EXTRACT_FFLAGS;
	}

	bsdtar->argv = argv;
	bsdtar->argc = argc;

	/* Process all remaining arguments now. */
	/*
	 * Comments following each option indicate where that option
	 * originated:  SUSv2, POSIX, GNU tar, star, etc.  If there's
	 * no such comment, then I don't know of anyone else who
	 * implements that option.
	 */
	while ((opt = bsdtar_getopt(bsdtar, tar_opts, &option)) != -1) {
		switch (opt) {
		case OPTION_AGGRESSIVE_NETWORKING: /* tarsnap */
			storage_aggressive_networking = 1;
			break;
		case 'B': /* GNU tar */
			/* libarchive doesn't need this; just ignore it. */
			break;
		case 'C': /* GNU tar */
			set_chdir(bsdtar, optarg);
			break;
		case 'c': /* SUSv2 */
			set_mode(bsdtar, opt, "-c");
			break;
		case OPTION_CACHEDIR: /* multitar */
			bsdtar->cachedir = optarg;
			break;
		case OPTION_CHECK_LINKS: /* GNU tar */
			bsdtar->option_warn_links = 1;
			break;
		case OPTION_CHROOT: /* NetBSD */
			bsdtar->option_chroot = 1;
			break;
		case 'd': /* multitar */
			set_mode(bsdtar, opt, "-d");
			break;
		case OPTION_EXCLUDE: /* GNU tar */
			if (exclude(bsdtar, optarg))
				bsdtar_errc(bsdtar, 1, 0,
				    "Couldn't exclude %s\n", optarg);
			break;
		case 'f': /* multitar */
			bsdtar->tapename = optarg;
			break;
		case OPTION_FSCK: /* multitar */
			set_mode(bsdtar, opt, "--fsck");
			break;
		case 'H': /* BSD convention */
			bsdtar->symlink_mode = 'H';
			break;
		case 'h': /* Linux Standards Base, gtar; synonym for -L */
			bsdtar->symlink_mode = 'L';
			/* Hack: -h by itself is the "help" command. */
			possible_help_request = 1;
			break;
		case OPTION_HELP: /* GNU tar, others */
			long_help(bsdtar);
			exit(0);
			break;
		case 'I': /* GNU tar */
			/*
			 * TODO: Allow 'names' to come from an archive,
			 * not just a text file.  Design a good UI for
			 * allowing names and mode/owner to be read
			 * from an archive, with contents coming from
			 * disk.  This can be used to "refresh" an
			 * archive or to design archives with special
			 * permissions without having to create those
			 * permissions on disk.
			 */
			bsdtar->names_from_file = optarg;
			break;
		case OPTION_INCLUDE:
			/*
			 * Noone else has the @archive extension, so
			 * noone else needs this to filter entries
			 * when transforming archives.
			 */
			if (include(bsdtar, optarg))
				bsdtar_errc(bsdtar, 1, 0,
				    "Failed to add %s to inclusion list",
				    optarg);
			break;
		case 'k': /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE;
			break;
		case OPTION_KEEP_NEWER_FILES: /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER;
			break;
		case OPTION_KEYFILE: /* tarsnap */
			load_keys(bsdtar, optarg);
			bsdtar->have_keys = 1;
			break;
		case 'L': /* BSD convention */
			bsdtar->symlink_mode = 'L';
			break;
	        case 'l': /* SUSv2 and GNU tar beginning with 1.16 */
			/* GNU tar 1.13  used -l for --one-file-system */
			bsdtar->option_warn_links = 1;
			break;
		case OPTION_LIST_ARCHIVES: /* multitar */
			set_mode(bsdtar, opt, "--list-archives");
			break;
		case OPTION_LOWMEM: /* tarsnap */
			bsdtar->cachecrunch = 1;
			break;
		case 'm': /* SUSv2 */
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_TIME;
			break;
		case 'n': /* GNU tar */
			bsdtar->option_no_subdirs = 1;
			break;
	        /*
		 * Selecting files by time:
		 *    --newer-?time='date' Only files newer than 'date'
		 *    --newer-?time-than='file' Only files newer than time
		 *         on specified file (useful for incremental backups)
		 * TODO: Add corresponding "older" options to reverse these.
		 */
		case OPTION_NEWER_CTIME: /* GNU tar */
			bsdtar->newer_ctime_sec = get_date(optarg);
			break;
		case OPTION_NEWER_CTIME_THAN:
			{
				struct stat st;
				if (stat(optarg, &st) != 0)
					bsdtar_errc(bsdtar, 1, 0,
					    "Can't open file %s", optarg);
				bsdtar->newer_ctime_sec = st.st_ctime;
				bsdtar->newer_ctime_nsec =
				    ARCHIVE_STAT_CTIME_NANOS(&st);
			}
			break;
		case OPTION_NEWER_MTIME: /* GNU tar */
			bsdtar->newer_mtime_sec = get_date(optarg);
			break;
		case OPTION_NEWER_MTIME_THAN:
			{
				struct stat st;
				if (stat(optarg, &st) != 0)
					bsdtar_errc(bsdtar, 1, 0,
					    "Can't open file %s", optarg);
				bsdtar->newer_mtime_sec = st.st_mtime;
				bsdtar->newer_mtime_nsec =
				    ARCHIVE_STAT_MTIME_NANOS(&st);
			}
			break;
		case OPTION_NODUMP: /* star */
			bsdtar->option_honor_nodump = 1;
			break;
		case OPTION_NO_SAME_OWNER: /* GNU tar */
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_OWNER;
			break;
		case OPTION_NO_SAME_PERMISSIONS: /* GNU tar */
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_PERM;
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_ACL;
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_XATTR;
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_FFLAGS;
			break;
		case OPTION_NULL: /* GNU tar */
			bsdtar->option_null++;
			break;
		case 'O': /* GNU tar */
			bsdtar->option_stdout = 1;
			break;
		case 'o':
			bsdtar->option_no_owner = 1;
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_OWNER;
			break;
		case OPTION_ONE_FILE_SYSTEM: /* GNU tar */
			bsdtar->option_dont_traverse_mounts = 1;
			break;
		case 'P': /* GNU tar */
			bsdtar->extract_flags &= ~SECURITY;
			bsdtar->option_absolute_paths = 1;
			break;
		case 'p': /* GNU tar, star */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_PERM;
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_ACL;
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_XATTR;
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_FFLAGS;
			break;
		case OPTION_PRINT_STATS: /* multitar */
			bsdtar->option_print_stats = 1;

			/*
			 * If we don't already have a mode set, we might be
			 * trying to print statistics without doing anything
			 * else.
			 */
			if (bsdtar->mode == '\0')
				bsdtar->mode = OPTION_PRINT_STATS;
			break;
		case 'q': /* FreeBSD GNU tar --fast-read, NetBSD -q */
			bsdtar->option_fast_read = 1;
			break;
		case 'r': /* multitar */
			set_mode(bsdtar, opt, "-r");
			break;
		case OPTION_SNAPTIME: /* multitar */
			{
				struct stat st;
				if (stat(optarg, &st) != 0)
					bsdtar_errc(bsdtar, 1, 0,
					    "Can't open file %s", optarg);
				bsdtar->snaptime = st.st_ctime;
			}
			break;
		case OPTION_STORE_ATIME: /* multitar */
			bsdtar->option_store_atime = 1;
			break;
		case OPTION_STRIP_COMPONENTS: /* GNU tar 1.15 */
			bsdtar->strip_components = atoi(optarg);
			break;
		case 'T': /* GNU tar */
			bsdtar->names_from_file = optarg;
			break;
		case 't': /* SUSv2 */
			set_mode(bsdtar, opt, "-t");
			bsdtar->verbose++;
			break;
		case OPTION_TOTALS: /* GNU tar */
			bsdtar->option_totals++;
			break;
		case 'U': /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_UNLINK;
			bsdtar->option_unlink_first = 1;
			break;
		case 'v': /* SUSv2 */
			bsdtar->verbose++;
			break;
		case OPTION_VERSION: /* GNU convention */
			version();
			break;
		case OPTION_VERYLOWMEM: /* tarsnap */
			bsdtar->cachecrunch = 2;
			break;
#if 0
		/*
		 * The -W longopt feature is handled inside of
		 * bsdtar_getop(), so -W is not available here.
		 */
		case 'W': /* Obscure, but useful GNU convention. */
			break;
#endif
		case 'w': /* SUSv2 */
			bsdtar->option_interactive = 1;
			break;
		case 'X': /* GNU tar */
			if (exclude_from_file(bsdtar, optarg))
				bsdtar_errc(bsdtar, 1, 0,
				    "failed to process exclusions from file %s",
				    optarg);
			break;
		case 'x': /* SUSv2 */
			set_mode(bsdtar, opt, "-x");
			break;
		default:
			usage(bsdtar);
		}
	}

	/* Read options from configuration files. */
	if ((homedir = getenv("HOME")) != NULL) {
		if (asprintf(&conffile, "%s/.tarsnaprc", homedir) == -1)
			bsdtar_errc(bsdtar, 1, errno, "No memory");

		configfile(bsdtar, conffile);

		/* Free string allocated by asprintf. */
		free(conffile);
	}
	configfile(bsdtar, ETC_TARSNAP_CONF);

	/*
	 * Sanity-check options.
	 */

	/* If no "real" mode was specified, treat -h as --help. */
	if ((bsdtar->mode == '\0') && possible_help_request) {
		long_help(bsdtar);
		exit(0);
	}

	if (bsdtar->mode == '\0')
		bsdtar_errc(bsdtar, 1, 0,
		    "Must specify one of -c, -d, -r, -t, -x,"
		    " --list-archives, or --print-stats");
	if ((bsdtar->tapename == NULL) &&
	    (bsdtar->mode != OPTION_PRINT_STATS &&
	     bsdtar->mode != OPTION_LIST_ARCHIVES &&
	     bsdtar->mode != OPTION_FSCK))
		bsdtar_errc(bsdtar, 1, 0,
		    "Archive name must be specified");
	if ((bsdtar->cachedir == NULL) &&
	    (bsdtar->mode == 'c' || bsdtar->mode == 'd' ||
	     bsdtar->mode == OPTION_FSCK ||
	     bsdtar->mode == OPTION_PRINT_STATS))
		bsdtar_errc(bsdtar, 1, 0,
		    "Cache directory must be specified for -c, -d,"
		    " --fsck, and --print-stats");
	if (bsdtar->have_keys == 0)
		bsdtar_errc(bsdtar, 1, 0,
		    "Keys must be provided via --keyfile option");

	/* The -f option doesn't make sense for --list-archives and --fsck. */
	if ((bsdtar->tapename != NULL) &&
	    (bsdtar->mode != OPTION_PRINT_STATS))
		only_mode(bsdtar, "-f", "cxtdr");

	/* These options don't make sense for "delete" and "convert to tar" */
	if (bsdtar->pending_chdir)
		only_mode(bsdtar, "-C", "cxt");
	if (bsdtar->names_from_file)
		only_mode(bsdtar, "-T", "cxt");
	if (bsdtar->newer_ctime_sec || bsdtar->newer_ctime_nsec)
		only_mode(bsdtar, "--newer", "cxt");
	if (bsdtar->newer_mtime_sec || bsdtar->newer_mtime_nsec)
		only_mode(bsdtar, "--newer-mtime", "cxt");
	if (bsdtar->option_absolute_paths)
		only_mode(bsdtar, "-P", "cxt");
	if (bsdtar->option_null)
		only_mode(bsdtar, "--null", "cxt");

	/* Check boolean options only permitted in certain modes. */
	if (storage_aggressive_networking)
		only_mode(bsdtar, "--aggressive-networking", "c");
	if (bsdtar->option_dont_traverse_mounts)
		only_mode(bsdtar, "--one-file-system", "c");
	if (bsdtar->option_fast_read)
		only_mode(bsdtar, "--fast-read", "xt");
	if (bsdtar->option_honor_nodump)
		only_mode(bsdtar, "--nodump", "c");
	if (bsdtar->option_no_owner)
		only_mode(bsdtar, "-o", "x");
	if (bsdtar->option_no_subdirs)
		only_mode(bsdtar, "-n", "c");
	if (bsdtar->option_print_stats &&
	    (bsdtar->mode != OPTION_PRINT_STATS))
		only_mode(bsdtar, "--print-stats", "cd");
	if (bsdtar->option_stdout)
		only_mode(bsdtar, "-O", "xt");
	if (bsdtar->option_store_atime)
		only_mode(bsdtar, "--store-atime", "c");
	if (bsdtar->option_totals)
		only_mode(bsdtar, "--totals", "c");
	if (bsdtar->option_unlink_first)
		only_mode(bsdtar, "-U", "x");
	if (bsdtar->option_warn_links)
		only_mode(bsdtar, "--check-links", "c");

	/* Check other parameters only permitted in certain modes. */
	if (bsdtar->symlink_mode != '\0') {
		strcpy(buff, "-?");
		buff[1] = bsdtar->symlink_mode;
		only_mode(bsdtar, buff, "c");
	}
	if (bsdtar->strip_components != 0)
		only_mode(bsdtar, "--strip-components", "xt");

	bsdtar->argc -= optind;
	bsdtar->argv += optind;

	/*
	 * Canonicalize the path to the cache directories.  This is
	 * necessary since the tar code can change directories.
	 */
	if (bsdtar->cachedir != NULL) {
		build_dir(bsdtar, bsdtar->cachedir, "--cachedir");
		if (realpath(bsdtar->cachedir, cachedir) == NULL)
			bsdtar_errc(bsdtar, 1, errno, "realpath(%s)",
			    bsdtar->cachedir);
		bsdtar->cachedir = cachedir;
	}

	/* Make sure we have whatever keys we're going to need. */
	missingkey = NULL;
	switch (bsdtar->mode) {
	case 'c':
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_WRITE);
		break;
	case 'd':
	case OPTION_FSCK:
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_READ |
		    CRYPTO_KEYMASK_AUTH_DELETE);
		break;
	case OPTION_PRINT_STATS:
		/* We don't need keys for printing global stats. */
		if (bsdtar->tapename == NULL)
			break;

		/* FALLTHROUGH */
	case OPTION_LIST_ARCHIVES:
	case 'r':
	case 't':
	case 'x':
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_READ);
		break;
	}
	if (missingkey != NULL)
		bsdtar_errc(bsdtar, 1, 0,
		    "The %s key is required for %s but is not available",
		    missingkey, bsdtar->modestr);

	/* Perform the requested operation. */
	switch(bsdtar->mode) {
	case 'c':
		tarsnap_mode_c(bsdtar);
		break;
	case 'd':
		tarsnap_mode_d(bsdtar);
		break;
	case OPTION_FSCK:
		tarsnap_mode_fsck(bsdtar);
		break;
	case OPTION_PRINT_STATS:
		tarsnap_mode_print_stats(bsdtar);
		break;
	case OPTION_LIST_ARCHIVES:
		tarsnap_mode_list_archives(bsdtar);
		break;
	case 'r':
		tarsnap_mode_r(bsdtar);
		break;
	case 't':
		tarsnap_mode_t(bsdtar);
		break;
	case 'x':
		tarsnap_mode_x(bsdtar);
		break;
	}

	cleanup_exclusions(bsdtar);

	/* Clean up network layer. */
	network_fini();

#ifdef PROFILE
	/*
	 * If we're compiling with profiling turned on, chdir to a directory
	 * into which we're likely to be able to write to before exiting.
	 */
	if (bsdtar->cachedir != NULL)
		chdir(cachedir);
#endif

	if (bsdtar->return_value != 0)
		bsdtar_warnc(bsdtar, 0,
		    "Error exit delayed from previous errors.");
	return (bsdtar->return_value);
}

static void
set_mode(struct bsdtar * bsdtar, int opt, const char *optstr)
{

	/* Make sure we're not asking tarsnap to do two things at once. */
	if (bsdtar->mode != 0 &&
	    bsdtar->mode != OPTION_PRINT_STATS &&
	    strcmp(bsdtar->modestr, optstr))
		bsdtar_errc(bsdtar, 1, 0,
		    "Can't specify both %s and %s", optstr, bsdtar->modestr);

	/* Set mode. */
	bsdtar->mode = opt;
	bsdtar->modestr = optstr;
}

/*
 * Verify that the mode is correct.
 */
static void
only_mode(struct bsdtar *bsdtar, const char *opt, const char *valid_modes)
{

	if (strchr(valid_modes, bsdtar->mode) == NULL)
		bsdtar_errc(bsdtar, 1, 0,
		    "Option %s is not permitted in mode %s",
		    opt, bsdtar->modestr);
}

void
usage(struct bsdtar *bsdtar)
{
	const char	*p;

	p = bsdtar->progname;

	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  List:       %s [options...] -tf <archive>\n", p);
	fprintf(stderr, "  Extract:    %s [options...] -xf <archive>\n", p);
	fprintf(stderr, "  Create:     %s [options...] -cf <archive>"
	     " [filenames...]\n", p);
	fprintf(stderr, "  Delete:     %s [options...] -df <archive>\n", p);
	fprintf(stderr, "  Tar output: %s [options...] -rf <archive>\n", p);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "  Help:    %s --help\n", p);
#else
	fprintf(stderr, "  Help:    %s -h\n", p);
#endif
	exit(1);
}

static void
version(void)
{
	printf("tarsnap %s\n", PACKAGE_VERSION);
	exit(1);
}

static const char *long_help_msg =
	"First option must be a mode specifier:\n"
	"  -c Create  -d Delete  -r Output as tar file  -t List  -x Extract\n"
	"Common Options:\n"
	"  -f <archive>  Archive name\n"
	"  --keyfile <file>        Key file\n"
	"  --cachedir <directory>  Cache directory\n"
	"  -v    Verbose\n"
	"  -w    Interactive\n"
	"Create: %p -c [options] [<file> | <dir> | @<archive> | -C <dir> ]\n"
	"  <file>, <dir>  add these items to archive\n"
#ifdef HAVE_GETOPT_LONG
	"  --exclude <pattern>  Skip files that match pattern\n"
#else
	"  -W exclude=<pattern>  Skip files that match pattern\n"
#endif
	"  -C <dir>  Change to <dir> before processing remaining files\n"
	"  @<filename>  Add entries from archive <filename>\n"
	"  @@ <archive>  Add entries from tarsnap archive <archive>\n"
	"List: %p -t [options] [<patterns>]\n"
	"  <patterns>  If specified, list only entries that match\n"
	"Extract: %p -x [options] [<patterns>]\n"
	"  <patterns>  If specified, extract only entries that match\n"
	"  -k    Keep (don't overwrite) existing files\n"
	"  -m    Don't restore modification times\n"
	"  -O    Write entries to stdout, don't restore to disk\n"
	"  -p    Restore permissions (including ACLs, owner, file flags)\n";


static void
long_help(struct bsdtar *bsdtar)
{
	const char	*prog;
	const char	*p;

	prog = bsdtar->progname;

	fflush(stderr);

	p = (strcmp(prog,"tarsnap") != 0) ? "(tarsnap)" : "";
	printf("%s%s: efficiently manipulate multiple archives\n", prog, p);

	for (p = long_help_msg; *p != '\0'; p++) {
		if (*p == '%') {
			if (p[1] == 'p') {
				fputs(prog, stdout);
				p++;
			} else
				putchar('%');
		} else
			putchar(*p);
	}
	version();
}

static int
bsdtar_getopt(struct bsdtar *bsdtar, const char *optstring,
    const struct option **poption)
{
	char *p, *q;
	const struct option *option;
	int opt;
	int option_index;
	size_t option_length;

	option_index = -1;
	*poption = NULL;

#ifdef HAVE_GETOPT_LONG
	opt = getopt_long(bsdtar->argc, bsdtar->argv, optstring,
	    tar_longopts, &option_index);
	if (option_index > -1)
		*poption = tar_longopts + option_index;
#else
	opt = getopt(bsdtar->argc, bsdtar->argv, optstring);
#endif

	/* Support long options through -W longopt=value */
	if (opt == 'W') {
		p = optarg;
		q = strchr(optarg, '=');
		if (q != NULL) {
			option_length = (size_t)(q - p);
			optarg = q + 1;
		} else {
			option_length = strlen(p);
			optarg = NULL;
		}
		option = tar_longopts;
		while (option->name != NULL &&
		    (strlen(option->name) < option_length ||
		    strncmp(p, option->name, option_length) != 0 )) {
			option++;
		}

		if (option->name != NULL) {
			*poption = option;
			opt = option->val;

			/* If the first match was exact, we're done. */
			if (strncmp(p, option->name, strlen(option->name)) == 0) {
				while (option->name != NULL)
					option++;
			} else {
				/* Check if there's another match. */
				option++;
				while (option->name != NULL &&
				    (strlen(option->name) < option_length ||
				    strncmp(p, option->name, option_length) != 0)) {
					option++;
				}
			}
			if (option->name != NULL)
				bsdtar_errc(bsdtar, 1, 0,
				    "Ambiguous option %s "
				    "(matches both %s and %s)",
				    p, (*poption)->name, option->name);

			if ((*poption)->has_arg == required_argument
			    && optarg == NULL)
				bsdtar_errc(bsdtar, 1, 0,
				    "Option \"%s\" requires argument", p);
		} else {
			opt = '?';
			/* TODO: Set up a fake 'struct option' for
			 * error reporting... ? ? ? */
		}
	}

	return (opt);
}

static void
build_dir(struct bsdtar *bsdtar, const char *dir, const char *diropt)
{
	struct stat sb;
	char * s;
	const char * dirseppos;

	/* Move through *dir and build all parent directories. */
	for (dirseppos = dir; *dirseppos != '\0'; ) {
		/* Move to the next '/', or the end of the string. */
		if ((dirseppos = strchr(dirseppos + 1, '/')) == NULL)
			dirseppos = dir + strlen(dir);

		/* Generate a string containing the parent directory. */
		if (asprintf(&s, "%.*s", (int)(dirseppos - dir), dir) == -1)
			bsdtar_errc(bsdtar, 1, errno, "No Memory");

		/* Does the parent directory exist already? */
		if (stat(s, &sb) == 0)
			goto nextdir;

		/* Did something go wrong? */
		if (errno != ENOENT)
			bsdtar_errc(bsdtar, 1, errno, "stat(%s)", s);

		/* Create the directory. */
		if (mkdir(s, 0700))
			bsdtar_errc(bsdtar, 1, errno, "error creating %s", s);

		/* Tell the user what we did. */
		fprintf(stderr, "Directory %s created for \"%s %s\"\n",
		    s, diropt, dir);

nextdir:
		free(s);
	}
}

/* Process options from the specified file, if it exists. */
static void
configfile(struct bsdtar *bsdtar, const char *fname)
{
	struct stat sb;

	/* If the file doesn't exist, do nothing. */
	if (stat(fname, &sb)) {
		if (errno == ENOENT)
			return;

		/*
		 * Something bad happened.  Note that this could occur if
		 * there is no configuration file and part of the path to
		 * where we're looking for a configuration file exists and
		 * is a non-directory (e.g., if /usr/local/etc is a file);
		 * we're going to error out if this happens, since reporting
		 * a spurious error in such an odd circumstance is better
		 * than failing to report an error if there really is a
		 * configuration file.
		 */
		bsdtar_errc(bsdtar, 1, errno, "stat(%s)", fname);
	}

	/* Process the file. */
	process_lines(bsdtar, fname, configfile_helper, 0);
}

/* Process a line of configuration file. */
static int
configfile_helper(struct bsdtar *bsdtar, const char *line)
{
	char * conf_opt;
	char * conf_arg;
	size_t optlen;

	/* Ignore comments and blank lines. */
	if ((line[0] == '#') || (line[0] == '\0'))
		return (0);

	/* Duplicate line. */
	if ((conf_opt = strdup(line)) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "Out of memory");

	/* Split line into option and argument if possible. */
	optlen = strcspn(conf_opt, " \t");

	/* Is there an argument? */
	if (conf_opt[optlen]) {
		/* NUL-terminate the option name. */
		conf_opt[optlen] = '\0';

		/* Find the start of the argument. */
		conf_arg = conf_opt + optlen + 1;
		conf_arg += strspn(conf_arg, " \t");

		/*
		 * If the line is whitespace-terminated, there might not be
		 * an argument here after all.
		 */
		if (conf_arg[0] == '\0')
			conf_arg = NULL;
	} else {
		/* No argument. */
		conf_arg = NULL;
	}

	if (strcmp(conf_opt, "cachedir") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if (bsdtar->cachedir == NULL)
			if ((bsdtar->cachedir = strdup(conf_arg)) == NULL)
				bsdtar_errc(bsdtar, 1, errno,
				    "Out of memory");
	} else if (strcmp(conf_opt, "exclude") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if (exclude(bsdtar, conf_arg))
			bsdtar_errc(bsdtar, 1, 0,
			    "Couldn't exclude %s", conf_arg);
	} else if (strcmp(conf_opt, "include") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if (include(bsdtar, conf_arg))
			bsdtar_errc(bsdtar, 1, 0,
			    "Failed to add %s to inclusion list", conf_arg);
	} else if (strcmp(conf_opt, "keyfile") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if (bsdtar->have_keys == 0) {
			load_keys(bsdtar, conf_arg);
			bsdtar->have_keys = 1;
		}
	} else if (strcmp(conf_opt, "nodump") == 0) {
		if (bsdtar->mode == 'c')
			bsdtar->option_honor_nodump = 1;
	} else if (strcmp(conf_opt, "print-stats") == 0) {
		if ((bsdtar->mode == 'c') || (bsdtar->mode == 'd'))
			bsdtar->option_print_stats = 1;
	} else if (strcmp(conf_opt, "snaptime") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if ((bsdtar->mode == 'c') && (bsdtar->snaptime == 0)) {
			struct stat st;

			if (stat(conf_arg, &st) != 0)
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't stat file %s", conf_arg);
			bsdtar->snaptime = st.st_ctime;
		}
	} else if (strcmp(conf_opt, "store-atime") == 0) {
		if (bsdtar->mode == 'c')
			bsdtar->option_store_atime = 1;
	} else if (strcmp(conf_opt, "totals") == 0) {
		if ((bsdtar->mode == 'c') && (bsdtar->option_totals == 0))
			bsdtar->option_totals++;
	} else {
		bsdtar_errc(bsdtar, 1, 0,
		    "Unrecognized configuration file option: \"%s\"",
		    conf_opt);
	}

	/* Free memory allocated by strdup. */
	free(conf_opt);

	return (0);
}

/* Load keys from the specified file. */
static void
load_keys(struct bsdtar *bsdtar, const char *path)
{
	struct stat sb;
	uint8_t * keybuf;
	FILE * f;

	/* Stat the file. */
	if (stat(path, &sb))
		bsdtar_errc(bsdtar, 1, errno, "stat(%s)", path);

	/* Allocate memory. */
	if ((sb.st_size < 8) || (sb.st_size > 1000000))
		bsdtar_errc(bsdtar, 1, 0,
		    "Key file has unreasonable size: %s", path);
	if ((keybuf = malloc(sb.st_size)) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "Out of memory");

	/* Read the file. */
	if ((f = fopen(path, "r")) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "fopen(%s)", path);
	if (fread(keybuf, sb.st_size, 1, f) != 1)
		bsdtar_errc(bsdtar, 1, errno, "fread(%s)", path);
	if (fclose(f))
		bsdtar_errc(bsdtar, 1, errno, "fclose(%s)", path);

	/* Parse machine number. */
	bsdtar->machinenum = be64dec(keybuf);

	/* Parse keys. */
	if (crypto_keys_import(&keybuf[8], sb.st_size - 8))
		bsdtar_errc(bsdtar, 1, errno,
		    "Error reading keys: %s", path);

	/* Free memory. */
	free(keybuf);
}
