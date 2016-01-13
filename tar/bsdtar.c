/*-
 * Copyright 2006-2009 Colin Percival
 * All rights reserved.
 *
 * Portions of the file below are covered by the following license:
 *
 * Copyright (c) 2003-2008 Tim Kientzle
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
__FBSDID("$FreeBSD: src/usr.bin/tar/bsdtar.c,v 1.93 2008/11/08 04:43:24 kientzle Exp $");

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
#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
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

#include <assert.h>

#include "bsdtar.h"
#include "crypto.h"
#include "humansize.h"
#include "keyfile.h"
#include "tarsnap_opt.h"
#include "tsnetwork.h"
#include "warnp.h"

/* Global tarsnap options declared in tarsnap_opt.h. */
int tarsnap_opt_aggressive_networking = 0;
int tarsnap_opt_humanize_numbers = 0;
int tarsnap_opt_noisy_warnings = 0;
uint64_t tarsnap_opt_checkpointbytes = (uint64_t)(-1);
uint64_t tarsnap_opt_maxbytesout = (uint64_t)(-1);

/* Structure for holding a delayed option. */
struct delayedopt {
	char * opt_name;
	char * opt_arg;
	struct delayedopt * next;
};

/* External function to parse a date/time string (from getdate.y) */
time_t get_date(time_t, const char *);

static struct bsdtar	*bsdtar_init(void);
static void		 bsdtar_atexit(void);

static void		 build_dir(struct bsdtar *, const char *dir,
			     const char *diropt);
static void		 configfile(struct bsdtar *, const char *fname);
static int		 configfile_helper(struct bsdtar *bsdtar,
			     const char *line);
static void		 dooption(struct bsdtar *, const char *,
			     const char *, int);
static void		 load_keys(struct bsdtar *, const char *path);
static void		 long_help(struct bsdtar *);
static void		 only_mode(struct bsdtar *, const char *opt,
			     const char *valid);
static void		 optq_push(struct bsdtar *, const char *,
			     const char *);
static void		 optq_pop(struct bsdtar *);
static void		 set_mode(struct bsdtar *, int opt, const char *optstr);
static void		 version(void);
static int		 argv_has_archive_directive(struct bsdtar *bsdtar);

/* A basic set of security flags to request from libarchive. */
#define	SECURITY					\
	(ARCHIVE_EXTRACT_SECURE_SYMLINKS		\
	 | ARCHIVE_EXTRACT_SECURE_NODOTDOT)

static struct bsdtar bsdtar_storage;

static struct bsdtar *
bsdtar_init(void)
{
	struct bsdtar * bsdtar = &bsdtar_storage;

	memset(bsdtar, 0, sizeof(*bsdtar));

	/*
	 * Initialize pointers.  memset() is insufficient since NULL is not
	 * required to be represented in memory by zeroes.
	 */
	bsdtar->tapenames = NULL;
	bsdtar->homedir = NULL;
	bsdtar->cachedir = NULL;
	bsdtar->pending_chdir = NULL;
	bsdtar->names_from_file = NULL;
	bsdtar->modestr = NULL;
	bsdtar->option_csv_filename = NULL;
	bsdtar->configfiles = NULL;
	bsdtar->archive = NULL;
	bsdtar->progname = NULL;
	bsdtar->argv = NULL;
	bsdtar->optarg = NULL;
	bsdtar->write_cookie = NULL;
	bsdtar->chunk_cache = NULL;
	bsdtar->argv_orig = NULL;
	bsdtar->delopt = NULL;
	bsdtar->delopt_tail = NULL;
	bsdtar->diskreader = NULL;
	bsdtar->resolver = NULL;
	bsdtar->gname_cache = NULL;
	bsdtar->buff = NULL;
	bsdtar->matching = NULL;
	bsdtar->security = NULL;
	bsdtar->uname_cache = NULL;
	bsdtar->siginfo = NULL;
	bsdtar->substitution = NULL;

	/* We don't have bsdtar->progname yet, so we can't use bsdtar_errc. */
	if (atexit(bsdtar_atexit)) {
		fprintf(stderr, "tarsnap: Could not register atexit.\n");
		exit(1);
	}

	return (bsdtar);
}

static void
bsdtar_atexit(void)
{
	struct bsdtar *bsdtar;

	bsdtar = &bsdtar_storage;

	/* Free arrays allocated by malloc. */
	free(bsdtar->tapenames);
	free(bsdtar->configfiles);

	/* Free strings allocated by strdup. */
	free(bsdtar->cachedir);
	free(bsdtar->homedir);
	free(bsdtar->option_csv_filename);

	/* Free matching and (if applicable) substitution patterns. */
	cleanup_exclusions(bsdtar);
#if HAVE_REGEX_H
	cleanup_substitution(bsdtar);
#endif

	/* Clean up network layer. */
	network_fini();
}

int
main(int argc, char **argv)
{
	struct bsdtar		*bsdtar;
	int			 opt;
	char			 possible_help_request;
	char			 buff[16];
	char			 cachedir[PATH_MAX + 1];
	struct passwd		*pws;
	char			*conffile;
	const char		*missingkey;
	time_t			 now;
	size_t 			 i;

	WARNP_INIT;

	/* Use a pointer for consistency. */
	bsdtar = bsdtar_init();

#if defined(_WIN32) && !defined(__CYGWIN__)
	/* Make sure open() function will be used with a binary mode. */
	/* on cygwin, we need something similar, but instead link against */
	/* a special startup object, binmode.o */
	_set_fmode(_O_BINARY);
#endif

	/* Need bsdtar->progname before calling bsdtar_warnc. */
	if (*argv == NULL)
		bsdtar->progname = "tarsnap";
	else {
#if defined(_WIN32) && !defined(__CYGWIN__)
		bsdtar->progname = strrchr(*argv, '\\');
#else
		bsdtar->progname = strrchr(*argv, '/');
#endif
		if (bsdtar->progname != NULL)
			bsdtar->progname++;
		else
			bsdtar->progname = *argv;
	}

	/* We don't have a machine # yet. */
	bsdtar->machinenum = (uint64_t)(-1);

	/* Allocate space for archive names; at most argc of them. */
	if ((bsdtar->tapenames = malloc(argc * sizeof(const char *))) == NULL)
		bsdtar_errc(bsdtar, 1, ENOMEM, "Cannot allocate memory");
	bsdtar->ntapes = 0;

	/* Allocate space for config file names; at most argc of them. */
	if ((bsdtar->configfiles = malloc(argc * sizeof(const char *))) == NULL)
		bsdtar_errc(bsdtar, 1, ENOMEM, "Cannot allocate memory");
	bsdtar->nconfigfiles = 0;

	time(&now);
	bsdtar->creationtime = now;

	if (setlocale(LC_ALL, "") == NULL)
		bsdtar_warnc(bsdtar, 0, "Failed to set default locale");
#if defined(HAVE_NL_LANGINFO) && defined(HAVE_D_MD_ORDER)
	bsdtar->day_first = (*nl_langinfo(D_MD_ORDER) == 'd');
#endif
	possible_help_request = 0;

	/* Initialize key cache.  We don't have any keys yet. */
	if (crypto_keys_init())
		exit(1);

	/*
	 * Make stdout line-buffered (if possible) so that operations such as
	 * "tarsnap --list-archives | more" will run more smoothly.  The only
	 * downside to this is a slight performance cost; but we don't write
	 * enough data to stdout for that to matter.
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

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

	/* Look up the current user and his home directory. */
	if ((pws = getpwuid(geteuid())) != NULL)
		if ((bsdtar->homedir = strdup(pws->pw_dir)) == NULL)
			bsdtar_errc(bsdtar, 1, ENOMEM, "Cannot allocate memory");

	/* Look up uid of current user for future reference */
	bsdtar->user_uid = geteuid();

	/* Default: preserve mod time on extract */
	bsdtar->extract_flags = ARCHIVE_EXTRACT_TIME;

	/* Default: Perform basic security checks. */
	bsdtar->extract_flags |= SECURITY;

	/* Defaults for root user: */
	if (bsdtar_is_privileged(bsdtar)) {
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

	/* We gather some options in a 'delayed options queue'. */
	bsdtar->delopt = NULL;
	bsdtar->delopt_tail = &bsdtar->delopt;

	/*
	 * Comments following each option indicate where that option
	 * originated:  SUSv2, POSIX, GNU tar, star, etc.  If there's
	 * no such comment, then I don't know of anyone else who
	 * implements that option.
	 */
	while ((opt = bsdtar_getopt(bsdtar)) != -1) {
		switch (opt) {
		case OPTION_AGGRESSIVE_NETWORKING: /* tarsnap */
			optq_push(bsdtar, "aggressive-networking", NULL);
			break;
		case 'B': /* GNU tar */
			/* libarchive doesn't need this; just ignore it. */
			break;
		case 'C': /* GNU tar */
			if (strlen(bsdtar->optarg) == 0)
				bsdtar_errc(bsdtar, 1, 0,
				    "Meaningless option: -C ''");

			set_chdir(bsdtar, bsdtar->optarg);
			break;
		case 'c': /* SUSv2 */
			set_mode(bsdtar, opt, "-c");
			break;
		case OPTION_CACHEDIR: /* multitar */
			optq_push(bsdtar, "cachedir", bsdtar->optarg);
			break;
		case OPTION_CHECK_LINKS: /* GNU tar */
			bsdtar->option_warn_links = 1;
			break;
		case OPTION_CHECKPOINT_BYTES: /* tarsnap */
			optq_push(bsdtar, "checkpoint-bytes", bsdtar->optarg);
			break;
		case OPTION_CHROOT: /* NetBSD */
			bsdtar->option_chroot = 1;
			break;
		case OPTION_CONFIGFILE:
			bsdtar->configfiles[bsdtar->nconfigfiles++] =
			    bsdtar->optarg;
			break;
		case OPTION_CREATIONTIME: /* tarsnap */
			errno = 0;
			bsdtar->creationtime = strtol(bsdtar->optarg,
			    NULL, 0);
			if ((errno) || (bsdtar->creationtime == 0))
				bsdtar_errc(bsdtar, 1, 0,
				    "Invalid --creationtime argument: %s",
				    bsdtar->optarg);
			break;
		case OPTION_CSV_FILE: /* tarsnap */
			if (bsdtar->option_csv_filename != NULL)
				bsdtar_errc(bsdtar, 1, errno,
				    "Two --csv-file options given.\n");
			if ((bsdtar->option_csv_filename = strdup(
			    bsdtar->optarg)) == NULL)
				bsdtar_errc(bsdtar, 1, errno, "Out of memory");
			break;
		case 'd': /* multitar */
			set_mode(bsdtar, opt, "-d");
			break;
		case OPTION_DISK_PAUSE: /* tarsnap */
			optq_push(bsdtar, "disk-pause", bsdtar->optarg);
			break;
		case OPTION_DRYRUN: /* tarsnap */
			bsdtar->option_dryrun = 1;
			break;
		case OPTION_EXCLUDE: /* GNU tar */
			optq_push(bsdtar, "exclude", bsdtar->optarg);
			break;
		case 'f': /* multitar */
			bsdtar->tapenames[bsdtar->ntapes++] = bsdtar->optarg;
			break;
		case OPTION_FSCK: /* multitar */
			set_mode(bsdtar, opt, "--fsck");
			break;
		case OPTION_FSCK_PRUNE: /* multitar */
			set_mode(bsdtar, opt, "--fsck-prune");
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
		case OPTION_HUMANIZE_NUMBERS: /* tarsnap */
			optq_push(bsdtar, "humanize-numbers", NULL);
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
			bsdtar->names_from_file = bsdtar->optarg;
			break;
		case OPTION_INCLUDE:
			optq_push(bsdtar, "include", bsdtar->optarg);
			break;
		case OPTION_INSANE_FILESYSTEMS:
			optq_push(bsdtar, "insane-filesystems", NULL);
			break;
		case 'k': /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE;
			break;
		case OPTION_KEEP_GOING: /* tarsnap */
			bsdtar->option_keep_going = 1;
			break;
		case OPTION_KEEP_NEWER_FILES: /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER;
			break;
		case OPTION_KEYFILE: /* tarsnap */
			optq_push(bsdtar, "keyfile", bsdtar->optarg);
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
			optq_push(bsdtar, "lowmem", NULL);
			break;
		case 'm': /* SUSv2 */
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_TIME;
			break;
		case OPTION_MAXBW: /* tarsnap */
			optq_push(bsdtar, "maxbw", bsdtar->optarg);
			break;
		case OPTION_MAXBW_RATE: /* tarsnap */
			optq_push(bsdtar, "maxbw-rate", bsdtar->optarg);
			break;
		case OPTION_MAXBW_RATE_DOWN: /* tarsnap */
			optq_push(bsdtar, "maxbw-rate-down", bsdtar->optarg);
			break;
		case OPTION_MAXBW_RATE_UP: /* tarsnap */
			optq_push(bsdtar, "maxbw-rate-up", bsdtar->optarg);
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
			bsdtar->newer_ctime_sec = get_date(now, bsdtar->optarg);
			break;
		case OPTION_NEWER_CTIME_THAN:
			{
				struct stat st;
				if (stat(bsdtar->optarg, &st) != 0)
					bsdtar_errc(bsdtar, 1, 0,
					    "Can't open file %s", bsdtar->optarg);
				bsdtar->newer_ctime_sec = st.st_ctime;
				bsdtar->newer_ctime_nsec =
				    ARCHIVE_STAT_CTIME_NANOS(&st);
			}
			break;
		case OPTION_NEWER_MTIME: /* GNU tar */
			bsdtar->newer_mtime_sec = get_date(now, bsdtar->optarg);
			break;
		case OPTION_NEWER_MTIME_THAN:
			{
				struct stat st;
				if (stat(bsdtar->optarg, &st) != 0)
					bsdtar_errc(bsdtar, 1, 0,
					    "Can't open file %s", bsdtar->optarg);
				bsdtar->newer_mtime_sec = st.st_mtime;
				bsdtar->newer_mtime_nsec =
				    ARCHIVE_STAT_MTIME_NANOS(&st);
			}
			break;
		case OPTION_NODUMP: /* star */
			optq_push(bsdtar, "nodump", NULL);
			break;
		case OPTION_NOISY_WARNINGS: /* tarsnap */
			tarsnap_opt_noisy_warnings = 1;
			break;
		case OPTION_NORMALMEM:
			optq_push(bsdtar, "normalmem", NULL);
			break;
		case OPTION_NO_AGGRESSIVE_NETWORKING:
			optq_push(bsdtar, "no-aggressive-networking", NULL);
			break;
		case OPTION_NO_CONFIG_EXCLUDE:
			optq_push(bsdtar, "no-config-exclude", NULL);
			break;
		case OPTION_NO_CONFIG_INCLUDE:
			optq_push(bsdtar, "no-config-include", NULL);
			break;
		case OPTION_NO_DEFAULT_CONFIG:
			bsdtar->option_no_default_config = 1;
			break;
		case OPTION_NO_DISK_PAUSE:
			optq_push(bsdtar, "no-disk-pause", NULL);
			break;
		case OPTION_NO_HUMANIZE_NUMBERS:
			optq_push(bsdtar, "no-humanize-numbers", NULL);
			break;
		case OPTION_NO_INSANE_FILESYSTEMS:
			optq_push(bsdtar, "no-insane-filesystems", NULL);
			break;
		case OPTION_NO_MAXBW:
			optq_push(bsdtar, "no-maxbw", NULL);
			break;
		case OPTION_NO_MAXBW_RATE_DOWN:
			optq_push(bsdtar, "no-maxbw-rate-down", NULL);
			break;
		case OPTION_NO_MAXBW_RATE_UP:
			optq_push(bsdtar, "no-maxbw-rate-up", NULL);
			break;
		case OPTION_NO_NODUMP:
			optq_push(bsdtar, "no-nodump", NULL);
			break;
		case OPTION_NO_PRINT_STATS:
			optq_push(bsdtar, "no-print-stats", NULL);
			break;
		case OPTION_NO_QUIET:
			optq_push(bsdtar, "no-quiet", NULL);
			break;
		case OPTION_NO_RETRY_FOREVER:
			optq_push(bsdtar, "no-retry-forever", NULL);
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
		case OPTION_NO_SNAPTIME:
			optq_push(bsdtar, "no-snaptime", NULL);
			break;
		case OPTION_NO_STORE_ATIME:
			optq_push(bsdtar, "no-store-atime", NULL);
			break;
		case OPTION_NO_TOTALS:
			optq_push(bsdtar, "no-totals", NULL);
			break;
		case OPTION_NUKE: /* tarsnap */
			set_mode(bsdtar, opt, "--nuke");
			break;
		case OPTION_NULL: /* GNU tar */
			bsdtar->option_null++;
			break;
		case OPTION_NUMERIC_OWNER: /* GNU tar */
			bsdtar->option_numeric_owner++;
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
			break;
		case 'q': /* FreeBSD GNU tar --fast-read, NetBSD -q */
			bsdtar->option_fast_read = 1;
			break;
		case OPTION_QUIET:
			optq_push(bsdtar, "quiet", NULL);
			break;
		case 'r': /* multitar */
			set_mode(bsdtar, opt, "-r");
			break;
		case OPTION_RECOVER:
			set_mode(bsdtar, opt, "--recover");
			break;
		case OPTION_RETRY_FOREVER:
			optq_push(bsdtar, "retry-forever", NULL);
			break;
		case OPTION_SNAPTIME: /* multitar */
			optq_push(bsdtar, "snaptime", bsdtar->optarg);
			break;
		case OPTION_STORE_ATIME: /* multitar */
			optq_push(bsdtar, "store-atime", NULL);
			break;
		case 'S': /* NetBSD pax-as-tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_SPARSE;
			break;
		case 's': /* NetBSD pax-as-tar */
#if HAVE_REGEX_H
			add_substitution(bsdtar, bsdtar->optarg);
#else
			bsdtar_warnc(bsdtar, 0,
			    "-s is not supported by this version of tarsnap");
			usage(bsdtar);
#endif
			break;
		case OPTION_SAME_OWNER: /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_OWNER;
			break;
		case OPTION_STRIP_COMPONENTS: /* GNU tar 1.15 */
			errno = 0;
			bsdtar->strip_components = strtol(bsdtar->optarg,
			    NULL, 0);
			if (errno)
				bsdtar_errc(bsdtar, 1, 0,
				    "Invalid --strip-components argument: %s",
				    bsdtar->optarg);
			break;
		case 'T': /* GNU tar */
			bsdtar->names_from_file = bsdtar->optarg;
			break;
		case 't': /* SUSv2 */
			set_mode(bsdtar, opt, "-t");
			bsdtar->verbose++;
			break;
		case OPTION_TOTALS: /* GNU tar */
			optq_push(bsdtar, "totals", NULL);
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
			optq_push(bsdtar, "verylowmem", NULL);
			break;
#if 0
		/*
		 * The -W longopt feature is handled inside of
		 * bsdtar_getopt(), so -W is not available here.
		 */
		case 'W': /* Obscure GNU convention. */
			break;
#endif
		case 'w': /* SUSv2 */
			bsdtar->option_interactive = 1;
			break;
		case 'X': /* GNU tar */
			if (exclude_from_file(bsdtar, bsdtar->optarg))
				bsdtar_errc(bsdtar, 1, 0,
				    "failed to process exclusions from file %s",
				    bsdtar->optarg);
			break;
		case 'x': /* SUSv2 */
			set_mode(bsdtar, opt, "-x");
			break;
		default:
			usage(bsdtar);
		}
	}

	/*
	 * Sanity-check options.
	 */

	/*
	 * If --print-stats was specified but no mode was set, then
	 * --print-stats *is* the mode.
	 */
	if ((bsdtar->mode == '\0') && (bsdtar->option_print_stats == 1))
		set_mode(bsdtar, OPTION_PRINT_STATS, "--print-stats");

	/* If no "real" mode was specified, treat -h as --help. */
	if ((bsdtar->mode == '\0') && possible_help_request) {
		long_help(bsdtar);
		exit(0);
	}

	/*
	 * If we're doing a dry run and the user hasn't specified an archive
	 * name via -f, use a fake name.  This will result in the statistics
	 * printed by --print-stats being a few bytes off, since the archive
	 * name is included in the metadata block... but we're going to be a
	 * few bytes off anyway since the command line, including "--dry-run"
	 * is included in the metadata.
	 */
	if (bsdtar->option_dryrun && (bsdtar->ntapes == 0))
		bsdtar->tapenames[bsdtar->ntapes++] = "(dry-run)";

	/* At this point we must have a mode set. */
	if (bsdtar->mode == '\0')
		bsdtar_errc(bsdtar, 1, 0,
		    "Must specify one of -c, -d, -r, -t, -x,"
		    " --list-archives, --print-stats,"
		    " --fsck, --fsck-prune, or --nuke");

	/* Process "delayed" command-line options which we queued earlier. */
	while (bsdtar->delopt != NULL) {
		dooption(bsdtar, bsdtar->delopt->opt_name,
		    bsdtar->delopt->opt_arg, 0);
		optq_pop(bsdtar);
	}

	/* Process config files passed on the command line. */
	for (i = 0; i < bsdtar->nconfigfiles; i++)
		configfile(bsdtar, bsdtar->configfiles[i]);

	/* If we do not have --no-default-config, process default configs. */
	if (bsdtar->option_no_default_config == 0) {
		/* Process options from ~/.tarsnaprc. */
		if (bsdtar->homedir != NULL) {
			if (asprintf(&conffile, "%s/.tarsnaprc",
			    bsdtar->homedir) == -1)
				bsdtar_errc(bsdtar, 1, errno, "No memory");

			configfile(bsdtar, conffile);

			/* Free string allocated by asprintf. */
			free(conffile);
		}

		/* Process options from system-wide tarsnap.conf. */
		configfile(bsdtar, ETC_TARSNAP_CONF);
	}

	/* Continue with more sanity-checking. */
	if ((bsdtar->ntapes == 0) &&
	    (bsdtar->mode != OPTION_PRINT_STATS &&
	     bsdtar->mode != OPTION_LIST_ARCHIVES &&
	     bsdtar->mode != OPTION_RECOVER &&
	     bsdtar->mode != OPTION_FSCK &&
	     bsdtar->mode != OPTION_FSCK_PRUNE &&
	     bsdtar->mode != OPTION_NUKE))
		bsdtar_errc(bsdtar, 1, 0,
		    "Archive name must be specified");
	if ((bsdtar->ntapes > 1) &&
	    (bsdtar->mode != OPTION_PRINT_STATS &&
	     bsdtar->mode != 'd'))
		bsdtar_errc(bsdtar, 1, 0,
		    "Option -f may only be specified once in mode %s",
		    bsdtar->modestr);
	if ((bsdtar->mode == 'c') &&
	    (strlen(bsdtar->tapenames[0]) > 1023))
		bsdtar_errc(bsdtar, 1, 0,
		    "Cannot create an archive with a name > 1023 characters");
	if ((bsdtar->mode == 'c') &&
	    (strlen(bsdtar->tapenames[0]) == 0))
		bsdtar_errc(bsdtar, 1, 0,
		    "Cannot create an archive with an empty name");
	if ((bsdtar->cachedir == NULL) &&
	    (((bsdtar->mode == 'c') && (!bsdtar->option_dryrun)) ||
	     bsdtar->mode == 'd' ||
	     bsdtar->mode == OPTION_RECOVER ||
	     bsdtar->mode == OPTION_FSCK ||
	     bsdtar->mode == OPTION_FSCK_PRUNE ||
	     bsdtar->mode == OPTION_PRINT_STATS))
		bsdtar_errc(bsdtar, 1, 0,
		    "Cache directory must be specified for %s",
		    bsdtar->modestr);
	if (tarsnap_opt_aggressive_networking != 0) {
		if ((bsdtar->bwlimit_rate_up != 0) ||
		    (bsdtar->bwlimit_rate_down != 0)) {
			bsdtar_warnc(bsdtar, 0,
			    "--aggressive-networking is incompatible with"
			    " --maxbw-rate options;\n"
			    "         disabling --aggressive-networking");
			tarsnap_opt_aggressive_networking = 0;
		}
	}

	/*
	 * The -f option doesn't make sense for --list-archives, --fsck,
	 * --fsck-prune, or --nuke.
	 */
	if ((bsdtar->ntapes > 0) &&
	    (bsdtar->mode != OPTION_PRINT_STATS))
		only_mode(bsdtar, "-f", "cxtdr");

	/*
	 * These options don't make sense for the "delete" and "convert to
	 * tar" modes.
	 */
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

	/* Check options only permitted in certain modes. */
	if (bsdtar->option_dont_traverse_mounts)
		only_mode(bsdtar, "--one-file-system", "c");
	if (bsdtar->option_fast_read)
		only_mode(bsdtar, "--fast-read", "xt");
	if (bsdtar->option_no_subdirs)
		only_mode(bsdtar, "-n", "c");
	if (bsdtar->option_no_owner)
		only_mode(bsdtar, "-o", "x");
	if (bsdtar->option_stdout)
		only_mode(bsdtar, "-O", "xt");
	if (bsdtar->option_unlink_first)
		only_mode(bsdtar, "-U", "x");
	if (bsdtar->option_warn_links)
		only_mode(bsdtar, "--check-links", "c");
	if (bsdtar->option_dryrun)
		only_mode(bsdtar, "--dry-run", "c");

	/* Check other parameters only permitted in certain modes. */
	if (bsdtar->symlink_mode != '\0') {
		strcpy(buff, "-?");
		buff[1] = bsdtar->symlink_mode;
		only_mode(bsdtar, buff, "c");
	}
	if (bsdtar->strip_components != 0)
		only_mode(bsdtar, "--strip-components", "xt");

	/*
	 * Canonicalize the path to the cache directories.  This is
	 * necessary since the tar code can change directories.
	 */
	if (bsdtar->cachedir != NULL) {
		build_dir(bsdtar, bsdtar->cachedir, "--cachedir");
		if (realpath(bsdtar->cachedir, cachedir) == NULL)
			bsdtar_errc(bsdtar, 1, errno, "realpath(%s)",
			    bsdtar->cachedir);
		free(bsdtar->cachedir);
		if ((bsdtar->cachedir = strdup(cachedir)) == NULL)
			bsdtar_errc(bsdtar, 1, errno, "Out of memory");
	}

	/* If we're running --fsck, figure out which key to use. */
	if (bsdtar->mode == OPTION_FSCK) {
		if (crypto_keys_missing(CRYPTO_KEYMASK_AUTH_PUT) == NULL)
			bsdtar->mode = OPTION_FSCK_WRITE;
		else if (crypto_keys_missing(CRYPTO_KEYMASK_AUTH_DELETE) == NULL)
			bsdtar->mode = OPTION_FSCK_DELETE;
		else
			bsdtar_errc(bsdtar, 1, 0,
			    "The write or delete authorization key is"
			    " required for --fsck but is not available");
	}

	/* If we're running --recover, figure out which key to use. */
	if (bsdtar->mode == OPTION_RECOVER) {
		if (crypto_keys_missing(CRYPTO_KEYMASK_AUTH_PUT) == NULL)
			bsdtar->mode = OPTION_RECOVER_WRITE;
		else if (crypto_keys_missing(CRYPTO_KEYMASK_AUTH_DELETE) == NULL)
			bsdtar->mode = OPTION_RECOVER_DELETE;
		else
			bsdtar_errc(bsdtar, 1, 0,
			    "The write or delete authorization key is"
			    " required for --recover but is not available");
	}

	/* Make sure we have whatever keys we're going to need. */
	if (bsdtar->have_keys == 0) {
		if (!bsdtar->option_dryrun) {
			bsdtar_errc(bsdtar, 1, 0,
			    "Keys must be provided via --keyfile option");
		} else {
			if (bsdtar->cachedir != NULL) {
				bsdtar_errc(bsdtar, 1, 0,
				    "Option mismatch for --dry-run: cachedir"
				    " specified but no keyfile");
			}
			if (crypto_keys_generate(CRYPTO_KEYMASK_USER))
				bsdtar_errc(bsdtar, 1, 0,
				    "Error generating keys");
			bsdtar_warnc(bsdtar, 0,
			    "Performing dry-run archival without keys\n"
			    "         (sizes may be slightly inaccurate)");
		}
	}

	missingkey = NULL;
	switch (bsdtar->mode) {
	case 'c':
		if (argv_has_archive_directive(bsdtar))
			missingkey = crypto_keys_missing(CRYPTO_KEYMASK_WRITE | CRYPTO_KEYMASK_READ);
		else
			missingkey = crypto_keys_missing(CRYPTO_KEYMASK_WRITE);
		break;
	case OPTION_RECOVER_WRITE:
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_WRITE);
		break;
	case 'd':
	case OPTION_FSCK_PRUNE:
	case OPTION_FSCK_DELETE:
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_READ |
		    CRYPTO_KEYMASK_AUTH_DELETE);
		break;
	case OPTION_FSCK_WRITE:
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_READ |
		    CRYPTO_KEYMASK_AUTH_PUT);
		break;
	case OPTION_NUKE:
	case OPTION_RECOVER_DELETE:
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_AUTH_DELETE);
		break;
	case OPTION_PRINT_STATS:
		/* We don't need keys for printing global stats. */
		if (bsdtar->ntapes == 0)
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

	/* Tell the network layer how much bandwidth to use. */
	if (bsdtar->bwlimit_rate_up == 0)
		bsdtar->bwlimit_rate_up = 1000000000.;
	if (bsdtar->bwlimit_rate_down == 0)
		bsdtar->bwlimit_rate_down = 1000000000.;
	network_bwlimit(bsdtar->bwlimit_rate_down, bsdtar->bwlimit_rate_up);

	/* Perform the requested operation. */
	switch(bsdtar->mode) {
	case 'c':
		tarsnap_mode_c(bsdtar);
		break;
	case 'd':
		tarsnap_mode_d(bsdtar);
		break;
	case OPTION_FSCK_DELETE:
		tarsnap_mode_fsck(bsdtar, 0, 1);
		break;
	case OPTION_FSCK_PRUNE:
		tarsnap_mode_fsck(bsdtar, 1, 1);
		break;
	case OPTION_FSCK_WRITE:
		tarsnap_mode_fsck(bsdtar, 0, 0);
		break;
	case OPTION_PRINT_STATS:
		tarsnap_mode_print_stats(bsdtar);
		break;
	case OPTION_RECOVER_DELETE:
		tarsnap_mode_recover(bsdtar, 1);
		break;
	case OPTION_RECOVER_WRITE:
		tarsnap_mode_recover(bsdtar, 0);
		break;
	case OPTION_LIST_ARCHIVES:
		tarsnap_mode_list_archives(bsdtar);
		break;
	case OPTION_NUKE:
		tarsnap_mode_nuke(bsdtar);
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

#ifdef DEBUG_SELECTSTATS
	double N, mu, va, max;

	network_getselectstats(&N, &mu, &va, &max);
	fprintf(stderr, "Time-between-select-calls statistics:\n");
	fprintf(stderr, "N = %6g   mu = %12g ms  "
	    "va = %12g ms^2  max = %12g ms\n",
	    N, mu * 1000, va * 1000000, max * 1000);
#endif

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
	if (bsdtar->mode != 0)
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
	fprintf(stderr, "  List:          %s [options...] -tf <archive>\n", p);
	fprintf(stderr, "  Extract:       %s [options...] -xf <archive>\n", p);
	fprintf(stderr, "  Create:        %s [options...] -cf <archive>"
	     " [filenames...]\n", p);
	fprintf(stderr, "  Delete:        %s [options...] -df <archive>\n", p);
	fprintf(stderr, "  Tar output:    %s [options...] -rf <archive>\n", p);
	fprintf(stderr, "  List archives: %s [options...] --list-archives\n", p);
	fprintf(stderr, "  Print stats:   %s [options...] --print-stats\n", p);
	fprintf(stderr, "  Help:    %s --help\n", p);
	exit(1);
}

static void
version(void)
{
	printf("tarsnap %s\n", PACKAGE_VERSION);
	exit(0);
}

static const char *long_help_msg =
	"First option must be a mode specifier:\n"
	"  -c Create  -d Delete  -r Output as tar file  -t List  -x Extract\n"
	"  --list-archives List archives  --print-stats Print archive statistics\n"
	"Common Options:\n"
	"  -f <archive>  Archive name\n"
	"  --keyfile <file>        Key file\n"
	"  --cachedir <directory>  Cache directory\n"
	"  -v    Verbose\n"
	"  -w    Interactive\n"
	"Create: %p -c [options] [<file> | <dir> | @@<archive> | -C <dir>] ...\n"
	"  <file>, <dir>  add these items to archive\n"
	"  --exclude <pattern>  Skip files that match pattern\n"
	"  -C <dir>  Change to <dir> before processing remaining files\n"
	"  @@<archive>  Add entries from tarsnap archive <archive>\n"
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

	p = (strcmp(prog, "tarsnap") != 0) ? "(tarsnap)" : "";
	printf("%s%s: create and manipulate archives on the Tarsnap backup service\n", prog, p);

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

static void
build_dir(struct bsdtar *bsdtar, const char *dir, const char *diropt)
{
	struct stat sb;
	char * s;
	const char * dirseppos;

	/* We need a directory name and the config option. */
	assert(dir != NULL);
	assert(diropt != NULL); 

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

	/* Make sure permissions on the directory are correct. */
	if (stat(dir, &sb))
		bsdtar_errc(bsdtar, 1, errno, "stat(%s)", dir);
	if (sb.st_mode & (S_IRWXG | S_IRWXO)) {
		if (chmod(dir, sb.st_mode & ~(S_IRWXG | S_IRWXO))) {
			bsdtar_errc(bsdtar, 1, errno,
			    "Cannot sanitize permissions on directory: %s",
			    dir);
		}
	}
}

/* Process options from the specified file, if it exists. */
static void
configfile(struct bsdtar *bsdtar, const char *fname)
{
	struct stat sb;

	/*
	 * If we had --no-config-exclude (or --no-config-include) earlier,
	 * we do not want to process any --exclude (or --include) options
	 * from now onwards.
	 */
	bsdtar->option_no_config_exclude_set =
	    bsdtar->option_no_config_exclude;
	bsdtar->option_no_config_include_set =
	    bsdtar->option_no_config_include;

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
	char * conf_arg_malloced;
	size_t len;

	/* Skip any leading whitespace. */
	while ((line[0] == ' ') || (line[0] == '\t'))
		line++;

	/* Ignore comments and blank lines. */
	if ((line[0] == '#') || (line[0] == '\0'))
		return (0);

	/* Duplicate line. */
	if ((conf_opt = strdup(line)) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "Out of memory");

	/*
	 * Detect any trailing whitespace.  This could happen before string
	 * duplication, but to reduce the number of diffs to a later version,
	 * we'll do it here.
	 */
	len = strlen(conf_opt);
	if ((len > 0) &&
	    ((conf_opt[len - 1] == ' ') || (conf_opt[len - 1] == '\t'))) {
		bsdtar_warnc(bsdtar, 0,
		    "option contains trailing whitespace; future behaviour"
		    " may change for:\n    %s", line);
	}

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

	/*
	 * If we have an argument which starts with ~, and ${HOME} is
	 * defined, expand ~ to $HOME.
	 */
	if ((conf_arg != NULL) && (conf_arg[0] == '~') &&
	    (bsdtar->homedir != NULL)) {
		/* Construct expanded argument string. */
		if (asprintf(&conf_arg_malloced, "%s%s",
		    bsdtar->homedir, &conf_arg[1]) == -1)
			bsdtar_errc(bsdtar, 1, errno, "Out of memory");

		/* Use the expanded argument string hereafter. */
		conf_arg = conf_arg_malloced;
	} else {
		conf_arg_malloced = NULL;
	}

	/* Process the configuration option. */
	dooption(bsdtar, conf_opt, conf_arg, 1);

	/* Free expanded argument or NULL. */
	free(conf_arg_malloced);

	/* Free memory allocated by strdup. */
	free(conf_opt);

	return (0);
}

/* Add a command-line option to the delayed options queue. */
static void
optq_push(struct bsdtar *bsdtar, const char * opt_name, const char * opt_arg)
{
	struct delayedopt * opt;

	/* Create a delayed option structure. */
	if ((opt = malloc(sizeof(struct delayedopt))) == NULL)
		goto enomem;
	if ((opt->opt_name = strdup(opt_name)) == NULL)
		goto enomem;
	if (opt_arg == NULL)
		opt->opt_arg = NULL;
	else if ((opt->opt_arg = strdup(opt_arg)) == NULL)
		goto enomem;
	opt->next = NULL;

	/* Add to queue. */
	*(bsdtar->delopt_tail) = opt;
	bsdtar->delopt_tail = &opt->next;

	/* Success! */
	return;

enomem:
	bsdtar_errc(bsdtar, 1, errno, "Out of memory");
}

/* Remove the first item from the delayed options queue. */
static void
optq_pop(struct bsdtar *bsdtar)
{
	struct delayedopt * opt = bsdtar->delopt;

	/* Remove from linked list. */
	bsdtar->delopt = opt->next;

	/* Free item. */
	free(opt->opt_name);
	free(opt->opt_arg);
	free(opt);
}

/* Process a line of configuration file or a command-line option. */
static void
dooption(struct bsdtar *bsdtar, const char * conf_opt,
    const char * conf_arg, int fromconffile)
{
	struct stat st;
	char *eptr;

	if (strcmp(conf_opt, "aggressive-networking") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_aggressive_networking_set)
			goto optset;

		tarsnap_opt_aggressive_networking = 1;
		bsdtar->option_aggressive_networking_set = 1;
	} else if (strcmp(conf_opt, "cachedir") == 0) {
		if (bsdtar->cachedir != NULL)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		if ((bsdtar->cachedir = strdup(conf_arg)) == NULL)
			bsdtar_errc(bsdtar, 1, errno, "Out of memory");
	} else if (strcmp(conf_opt, "checkpoint-bytes") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (tarsnap_opt_checkpointbytes != (uint64_t)(-1))
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		if (humansize_parse(conf_arg, &tarsnap_opt_checkpointbytes))
			bsdtar_errc(bsdtar, 1, 0,
			    "Cannot parse #bytes per checkpoint: %s",
			    conf_arg);
		if (tarsnap_opt_checkpointbytes < 1000000)
			bsdtar_errc(bsdtar, 1, 0,
			    "checkpoint-bytes value must be at least 1M");
	} else if (strcmp(conf_opt, "disk-pause") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_disk_pause_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		bsdtar->disk_pause = strtol(conf_arg, NULL, 0);
		bsdtar->option_disk_pause_set = 1;
	} else if (strcmp(conf_opt, "exclude") == 0) {
		if (bsdtar->option_no_config_exclude_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		if (exclude(bsdtar, conf_arg))
			bsdtar_errc(bsdtar, 1, 0,
			    "Couldn't exclude %s", conf_arg);
	} else if (strcmp(conf_opt, "humanize-numbers") == 0) {
		if (bsdtar->option_humanize_numbers_set)
			goto optset;

		tarsnap_opt_humanize_numbers = 1;
		bsdtar->option_humanize_numbers_set = 1;
	} else if (strcmp(conf_opt, "include") == 0) {
		if (bsdtar->option_no_config_include_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		if (include(bsdtar, conf_arg))
			bsdtar_errc(bsdtar, 1, 0,
			    "Failed to add %s to inclusion list", conf_arg);
	} else if (strcmp(conf_opt, "insane-filesystems") == 0) {
		if (bsdtar->option_insane_filesystems_set)
			goto optset;

		bsdtar->option_insane_filesystems = 1;
		bsdtar->option_insane_filesystems_set = 1;
	} else if (strcmp(conf_opt, "keyfile") == 0) {
		if (bsdtar->have_keys)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		load_keys(bsdtar, conf_arg);
		bsdtar->have_keys = 1;
	} else if (strcmp(conf_opt, "lowmem") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_cachecrunch_set)
			goto optset;

		bsdtar->cachecrunch = 1;
		bsdtar->option_cachecrunch_set = 1;
	} else if (strcmp(conf_opt, "maxbw") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_maxbw_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		if (humansize_parse(conf_arg, &tarsnap_opt_maxbytesout))
			bsdtar_errc(bsdtar, 1, 0,
			    "Cannot parse bandwidth limit: %s", conf_arg);
		bsdtar->option_maxbw_set = 1;
	} else if (strcmp(conf_opt, "maxbw-rate") == 0) {
		dooption(bsdtar, "maxbw-rate-down", conf_arg, fromconffile);
		dooption(bsdtar, "maxbw-rate-up", conf_arg, fromconffile);
	} else if (strcmp(conf_opt, "maxbw-rate-down") == 0) {
		if (bsdtar->option_maxbw_rate_down_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		bsdtar->bwlimit_rate_down = strtod(conf_arg, &eptr);
		if ((*eptr != '\0') ||
		    (bsdtar->bwlimit_rate_down < 8000) ||
		    (bsdtar->bwlimit_rate_down > 1000000000.))
			bsdtar_errc(bsdtar, 1, 0,
			    "Invalid bandwidth rate limit: %s", conf_arg);
		bsdtar->option_maxbw_rate_down_set = 1;
	} else if (strcmp(conf_opt, "maxbw-rate-up") == 0) {
		if (bsdtar->option_maxbw_rate_up_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		bsdtar->bwlimit_rate_up = strtod(conf_arg, &eptr);
		if ((*eptr != '\0') ||
		    (bsdtar->bwlimit_rate_up < 8000) ||
		    (bsdtar->bwlimit_rate_up > 1000000000.))
			bsdtar_errc(bsdtar, 1, 0,
			    "Invalid bandwidth rate limit: %s", conf_arg);
		bsdtar->option_maxbw_rate_up_set = 1;
	} else if (strcmp(conf_opt, "nodump") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_nodump_set)
			goto optset;

		bsdtar->option_honor_nodump = 1;
		bsdtar->option_nodump_set = 1;
	} else if (strcmp(conf_opt, "normalmem") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_cachecrunch_set)
			goto optset;

		bsdtar->option_cachecrunch_set = 1;
	} else if (strcmp(conf_opt, "no-aggressive-networking") == 0) {
		if (bsdtar->option_aggressive_networking_set)
			goto optset;

		bsdtar->option_aggressive_networking_set = 1;
	} else if (strcmp(conf_opt, "no-config-exclude") == 0) {
		if (bsdtar->option_no_config_exclude)
			goto optset;

		bsdtar->option_no_config_exclude = 1;
	} else if (strcmp(conf_opt, "no-config-include") == 0) {
		if (bsdtar->option_no_config_include)
			goto optset;

		bsdtar->option_no_config_include = 1;
	} else if (strcmp(conf_opt, "no-disk-pause") == 0) {
		if (bsdtar->option_disk_pause_set)
			goto optset;

		bsdtar->option_disk_pause_set = 1;
	} else if (strcmp(conf_opt, "no-humanize-numbers") == 0) {
		if (bsdtar->option_humanize_numbers_set)
			goto optset;

		bsdtar->option_humanize_numbers_set = 1;
	} else if (strcmp(conf_opt, "no-insane-filesystems") == 0) {
		if (bsdtar->option_insane_filesystems_set)
			goto optset;

		bsdtar->option_insane_filesystems_set = 1;
	} else if (strcmp(conf_opt, "no-maxbw") == 0) {
		if (bsdtar->option_maxbw_set)
			goto optset;

		bsdtar->option_maxbw_set = 1;
	} else if (strcmp(conf_opt, "no-maxbw-rate-down") == 0) {
		if (bsdtar->option_maxbw_rate_down_set)
			goto optset;

		bsdtar->option_maxbw_rate_down_set = 1;
	} else if (strcmp(conf_opt, "no-maxbw-rate-up") == 0) {
		if (bsdtar->option_maxbw_rate_up_set)
			goto optset;

		bsdtar->option_maxbw_rate_up_set = 1;
	} else if (strcmp(conf_opt, "no-nodump") == 0) {
		if (bsdtar->option_nodump_set)
			goto optset;

		bsdtar->option_nodump_set = 1;
	} else if (strcmp(conf_opt, "no-print-stats") == 0) {
		if (bsdtar->option_print_stats_set)
			goto optset;

		bsdtar->option_print_stats_set = 1;
	} else if (strcmp(conf_opt, "no-quiet") == 0) {
		if (bsdtar->option_quiet_set)
			goto optset;

		bsdtar->option_quiet_set = 1;
	} else if (strcmp(conf_opt, "no-retry-forever") == 0) {
		if (bsdtar->option_retry_forever_set)
			goto optset;

		bsdtar->option_retry_forever_set = 1;
	} else if (strcmp(conf_opt, "no-snaptime") == 0) {
		if (bsdtar->option_snaptime_set)
			goto optset;

		bsdtar->option_snaptime_set = 1;
	} else if (strcmp(conf_opt, "no-store-atime") == 0) {
		if (bsdtar->option_store_atime_set)
			goto optset;

		bsdtar->option_store_atime_set = 1;
	} else if (strcmp(conf_opt, "no-totals") == 0) {
		if (bsdtar->option_totals_set)
			goto optset;

		bsdtar->option_totals_set = 1;
	} else if (strcmp(conf_opt, "print-stats") == 0) {
		if ((bsdtar->mode != 'c') && (bsdtar->mode != 'd'))
			goto badmode;
		if (bsdtar->option_print_stats_set)
			goto optset;

		bsdtar->option_print_stats = 1;
		bsdtar->option_print_stats_set = 1;
	} else if (strcmp(conf_opt, "quiet") == 0) {
		if (bsdtar->option_quiet_set)
			goto optset;

		bsdtar->option_quiet = 1;
		bsdtar->option_quiet_set = 1;
	} else if (strcmp(conf_opt, "retry-forever") == 0) {
		if (bsdtar->option_retry_forever_set)
			goto optset;

		tarsnap_opt_retry_forever = 1;
		bsdtar->option_retry_forever_set = 1;
	} else if (strcmp(conf_opt, "snaptime") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_snaptime_set)
			goto optset;
		if (conf_arg == NULL)
			goto needarg;

		if (stat(conf_arg, &st) != 0)
			bsdtar_errc(bsdtar, 1, 0,
			    "Can't stat file %s", conf_arg);
		bsdtar->snaptime = st.st_ctime;
		bsdtar->option_snaptime_set = 1;
	} else if (strcmp(conf_opt, "store-atime") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_store_atime_set)
			goto optset;

		bsdtar->option_store_atime = 1;
		bsdtar->option_store_atime_set = 1;
	} else if (strcmp(conf_opt, "totals") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_totals_set)
			goto optset;

		bsdtar->option_totals = 1;
		bsdtar->option_totals_set = 1;
	} else if (strcmp(conf_opt, "verylowmem") == 0) {
		if (bsdtar->mode != 'c')
			goto badmode;
		if (bsdtar->option_cachecrunch_set)
			goto optset;

		bsdtar->cachecrunch = 2;
		bsdtar->option_cachecrunch_set = 1;
	} else {
		goto badopt;
	}
	return;

badmode:
	/* Option not relevant in this mode. */
	if (fromconffile == 0) {
		bsdtar_errc(bsdtar, 1, 0,
		    "Option --%s is not permitted in mode %s",
		    conf_opt, bsdtar->modestr);
	}
	return;

optset:
	/* Option specified multiple times. */
	if (fromconffile == 0) {
		usage(bsdtar);
	}
	return;

needarg:
	/* Option needs an argument. */
	bsdtar_errc(bsdtar, 1, 0,
	    "Argument required for configuration file option: %s", conf_opt);

badopt:
	/* No such option. */
	bsdtar_errc(bsdtar, 1, 0,
	    "Unrecognized configuration file option: \"%s\"", conf_opt);
}

/* Load keys from the specified file. */
static void
load_keys(struct bsdtar *bsdtar, const char *path)
{
	uint64_t machinenum;

	/* Load the key file. */
	if (keyfile_read(path, &machinenum, ~0))
		bsdtar_errc(bsdtar, 1, errno,
		    "Cannot read key file: %s", path);

	/* Check the machine number. */
	if ((bsdtar->machinenum != (uint64_t)(-1)) &&
	    (machinenum != bsdtar->machinenum))
		bsdtar_errc(bsdtar, 1, 0,
		    "Key file belongs to wrong machine: %s", path);
	bsdtar->machinenum = machinenum;
}

static int
argv_has_archive_directive(struct bsdtar *bsdtar)
{
	int i;
	const char *arg;

	/* Find "@@*", but don't trigger on "-C @@foo". */
	for (i = 0; i < bsdtar->argc; i++) {
		/* Improves code legibility. */
		arg = bsdtar->argv[i];

		/* Detect "-C" by itself. */
		if ((arg[0] == '-') && (arg[1] == 'C') && (arg[2] == '\0')) {
			i++;
			continue;
		}

		/* Detect any remaining "@@*". */
		if ((arg[0] == '@') && (arg[1] == '@')) {
			return (1);
		}
	}

	return (0);
}
