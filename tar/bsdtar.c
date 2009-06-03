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
#include "humansize.h"
#include "network.h"
#include "tarsnap_opt.h"
#include "warnp.h"

/* Global tarsnap options declared in tarsnap_opt.h. */
int tarsnap_opt_aggressive_networking = 0;
int tarsnap_opt_humanize_numbers = 0;
int tarsnap_opt_noisy_warnings = 0;
uint64_t tarsnap_opt_checkpointbytes = (uint64_t)(-1);
uint64_t tarsnap_opt_maxbytesout = (uint64_t)(-1);

/* External function to parse a date/time string (from getdate.y) */
time_t get_date(const char *);

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

/* A basic set of security flags to request from libarchive. */
#define	SECURITY					\
	(ARCHIVE_EXTRACT_SECURE_SYMLINKS		\
	 | ARCHIVE_EXTRACT_SECURE_NODOTDOT)

int
main(int argc, char **argv)
{
	struct bsdtar		*bsdtar, bsdtar_storage;
	int			 opt;
	char			 possible_help_request;
	char			 buff[16];
	char			 cachedir[PATH_MAX + 1];
	char			*homedir;
	char			*conffile;
	const char		*missingkey;
	char			*eptr;

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
#ifdef NEED_WARN_PROGNAME
	warn_progname = bsdtar->progname;
#endif

	/* We don't have a machine # yet. */
	bsdtar->machinenum = (uint64_t)(-1);

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

	/*
	 * Comments following each option indicate where that option
	 * originated:  SUSv2, POSIX, GNU tar, star, etc.  If there's
	 * no such comment, then I don't know of anyone else who
	 * implements that option.
	 */
	while ((opt = bsdtar_getopt(bsdtar)) != -1) {
		switch (opt) {
		case OPTION_AGGRESSIVE_NETWORKING: /* tarsnap */
			tarsnap_opt_aggressive_networking = 1;
			break;
		case 'B': /* GNU tar */
			/* libarchive doesn't need this; just ignore it. */
			break;
		case 'C': /* GNU tar */
			set_chdir(bsdtar, bsdtar->optarg);
			break;
		case 'c': /* SUSv2 */
			set_mode(bsdtar, opt, "-c");
			break;
		case OPTION_CACHEDIR: /* multitar */
			bsdtar->cachedir = bsdtar->optarg;
			break;
		case OPTION_CHECK_LINKS: /* GNU tar */
			bsdtar->option_warn_links = 1;
			break;
		case OPTION_CHECKPOINT_BYTES: /* tarsnap */
			if (humansize_parse(bsdtar->optarg,
			    &tarsnap_opt_checkpointbytes))
				bsdtar_errc(bsdtar, 1, 0,
				    "Cannot parse #bytes per checkpoint: %s",
				    bsdtar->optarg);
			if (tarsnap_opt_checkpointbytes < 1000000)
				bsdtar_errc(bsdtar, 1, 0,
				    "--checkpoint-bytes value must be at "
				    "least 1M");
			break;
		case OPTION_CHROOT: /* NetBSD */
			bsdtar->option_chroot = 1;
			break;
		case 'd': /* multitar */
			set_mode(bsdtar, opt, "-d");
			break;
		case OPTION_DRYRUN: /* tarsnap */
			bsdtar->option_dryrun = 1;
			break;
		case OPTION_EXCLUDE: /* GNU tar */
			if (exclude(bsdtar, bsdtar->optarg))
				bsdtar_errc(bsdtar, 1, 0,
				    "Couldn't exclude %s\n", bsdtar->optarg);
			break;
		case 'f': /* multitar */
			bsdtar->tapename = bsdtar->optarg;
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
		case OPTION_HUMANIZE_NUMBERS: /* tarsnap */
			tarsnap_opt_humanize_numbers = 1;
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
			/*
			 * Noone else has the @archive extension, so
			 * noone else needs this to filter entries
			 * when transforming archives.
			 */
			if (include(bsdtar, bsdtar->optarg))
				bsdtar_errc(bsdtar, 1, 0,
				    "Failed to add %s to inclusion list",
				    bsdtar->optarg);
			break;
		case 'k': /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE;
			break;
		case OPTION_KEEP_NEWER_FILES: /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER;
			break;
		case OPTION_KEYFILE: /* tarsnap */
			load_keys(bsdtar, bsdtar->optarg);
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
		case OPTION_MAXBW: /* tarsnap */
			if (humansize_parse(bsdtar->optarg,
			    &tarsnap_opt_maxbytesout))
				bsdtar_errc(bsdtar, 1, 0,
				    "Cannot parse bandwidth limit: %s",
				    bsdtar->optarg);
			break;
		case OPTION_MAXBW_RATE: /* tarsnap */
			bsdtar->bwlimit_rate_up = bsdtar->bwlimit_rate_down =
			    strtod(bsdtar->optarg, &eptr);
			if ((*eptr != '\0') ||
			    (bsdtar->bwlimit_rate_up < 8000) ||
			    (bsdtar->bwlimit_rate_up > 1000000000.))
				bsdtar_errc(bsdtar, 1, 0,
				    "Invalid bandwidth rate limit: %s",
				    bsdtar->optarg);
			break;
		case OPTION_MAXBW_RATE_DOWN: /* tarsnap */
			bsdtar->bwlimit_rate_down =
			    strtod(bsdtar->optarg, &eptr);
			if ((*eptr != '\0') ||
			    (bsdtar->bwlimit_rate_down < 8000) ||
			    (bsdtar->bwlimit_rate_down > 1000000000.))
				bsdtar_errc(bsdtar, 1, 0,
				    "Invalid bandwidth rate limit: %s",
				    bsdtar->optarg);
			break;
		case OPTION_MAXBW_RATE_UP: /* tarsnap */
			bsdtar->bwlimit_rate_up =
			    strtod(bsdtar->optarg, &eptr);
			if ((*eptr != '\0') ||
			    (bsdtar->bwlimit_rate_up < 8000) ||
			    (bsdtar->bwlimit_rate_up > 1000000000.))
				bsdtar_errc(bsdtar, 1, 0,
				    "Invalid bandwidth rate limit: %s",
				    bsdtar->optarg);
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
			bsdtar->newer_ctime_sec = get_date(bsdtar->optarg);
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
			bsdtar->newer_mtime_sec = get_date(bsdtar->optarg);
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
			bsdtar->option_honor_nodump = 1;
			break;
		case OPTION_NOISY_WARNINGS: /* tarsnap */
			tarsnap_opt_noisy_warnings = 1;
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
				if (stat(bsdtar->optarg, &st) != 0)
					bsdtar_errc(bsdtar, 1, 0,
					    "Can't open file %s",
					    bsdtar->optarg);
				bsdtar->snaptime = st.st_ctime;
			}
			break;
		case OPTION_STORE_ATIME: /* multitar */
			bsdtar->option_store_atime = 1;
			break;
		case 'S': /* NetBSD pax-as-tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_SPARSE;
			break;
		case 's': /* NetBSD pax-as-tar */
#if HAVE_REGEX_H
			add_substitution(bsdtar, bsdtar->optarg);
#else
			bsdtar_warnc(bsdtar, 0,
			    "-s is not supported by this version of bsdtar");
			usage(bsdtar);
#endif
			break;
		case OPTION_STRIP_COMPONENTS: /* GNU tar 1.15 */
			bsdtar->strip_components = atoi(bsdtar->optarg);
			break;
		case 'T': /* GNU tar */
			bsdtar->names_from_file = bsdtar->optarg;
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
	 * Start with basic checks which can be done prior to reading
	 * configuration files; the configuration file reading might error
	 * out, and if someone asks for help they should get that rather than
	 * a configuration file related error message.
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

	/* Read options from configuration files. */
	if ((homedir = getenv("HOME")) != NULL) {
		if (asprintf(&conffile, "%s/.tarsnaprc", homedir) == -1)
			bsdtar_errc(bsdtar, 1, errno, "No memory");

		configfile(bsdtar, conffile);

		/* Free string allocated by asprintf. */
		free(conffile);
	}
	configfile(bsdtar, ETC_TARSNAP_CONF);

	/* Continue with more sanity-checking. */
	if ((bsdtar->tapename == NULL) &&
	    (bsdtar->mode != OPTION_PRINT_STATS &&
	     bsdtar->mode != OPTION_LIST_ARCHIVES &&
	     bsdtar->mode != OPTION_FSCK &&
	     bsdtar->mode != OPTION_NUKE))
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
	if (tarsnap_opt_aggressive_networking != 0) {
		if ((bsdtar->bwlimit_rate_up != 0) ||
		    (bsdtar->bwlimit_rate_down != 0)) {
			bsdtar_errc(bsdtar, 1, 0,
			    "--aggressive-networking is incompatible with"
			    " --maxbw-rate options");
		}
	}

	/*
	 * The -f option doesn't make sense for --list-archives, --fsck, or
	 * --nuke.
	 */
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

	/* Check options only permitted in certain modes. */
	if (tarsnap_opt_aggressive_networking)
		only_mode(bsdtar, "--aggressive-networking", "c");
	if (tarsnap_opt_maxbytesout != (uint64_t)(-1))
		only_mode(bsdtar, "--maxbw", "c");
	if (tarsnap_opt_checkpointbytes != (uint64_t)(-1))
		only_mode(bsdtar, "--checkpoint-bytes", "c");
	if (bsdtar->option_dont_traverse_mounts)
		only_mode(bsdtar, "--one-file-system", "c");
	if (bsdtar->option_dryrun)
		only_mode(bsdtar, "--dry-run", "c");
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
	case OPTION_NUKE:
		missingkey = crypto_keys_missing(CRYPTO_KEYMASK_AUTH_DELETE);
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
	case OPTION_FSCK:
		tarsnap_mode_fsck(bsdtar);
		break;
	case OPTION_PRINT_STATS:
		tarsnap_mode_print_stats(bsdtar);
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

	cleanup_exclusions(bsdtar);
#if HAVE_REGEX_H
	cleanup_substitution(bsdtar);
#endif

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
	fprintf(stderr, "  Help:    %s --help\n", p);
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
	"  --exclude <pattern>  Skip files that match pattern\n"
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
	char * homedir;
	char * conf_arg_malloced;

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

	/*
	 * If we have an argument which starts with ~, and ${HOME} is
	 * defined, expand ~ to $HOME.
	 */
	if ((conf_arg != NULL) && (conf_arg[0] == '~') &&
	    ((homedir = getenv("HOME")) != NULL)) {
		/* Allocate space for the expanded argument string. */
		if ((conf_arg_malloced =
		    malloc(strlen(conf_arg) + strlen(homedir))) == NULL)
			bsdtar_errc(bsdtar, 1, errno, "Out of memory");

		/* Copy $HOME and the argument sans leading ~. */
		memcpy(conf_arg_malloced, homedir, strlen(homedir));
		memcpy(conf_arg_malloced + strlen(homedir), &conf_arg[1],
		    strlen(&conf_arg[1]) + 1);

		/* Use the expanded argument string hereafter. */
		conf_arg = conf_arg_malloced;
	} else {
		conf_arg_malloced = NULL;
	}

	if (strcmp(conf_opt, "aggressive-networking") == 0) {
		tarsnap_opt_aggressive_networking = 1;
	} else if (strcmp(conf_opt, "cachedir") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if (bsdtar->cachedir == NULL)
			if ((bsdtar->cachedir = strdup(conf_arg)) == NULL)
				bsdtar_errc(bsdtar, 1, errno,
				    "Out of memory");
	} else if (strcmp(conf_opt, "checkpoint-bytes") == 0) {
		if (conf_arg == NULL)
			bsdtar_errc(bsdtar, 1, 0,
			    "Argument required for "
			    "configuration file option: %s", conf_opt);
		if ((bsdtar->mode == 'c') &&
		    (tarsnap_opt_checkpointbytes == (uint64_t)(-1))) {
			if (humansize_parse(conf_arg,
			    &tarsnap_opt_checkpointbytes))
				bsdtar_errc(bsdtar, 1, 0,
				    "Cannot parse #bytes per checkpoint: %s",
				    bsdtar->optarg);
			if (tarsnap_opt_checkpointbytes < 1000000)
				bsdtar_errc(bsdtar, 1, 0,
				    "checkpoint-bytes value must be at "
				    "least 1M");
		}
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
	} else if (strcmp(conf_opt, "lowmem") == 0) {
		if ((bsdtar->mode == 'c') && (bsdtar->cachecrunch == 0))
			bsdtar->cachecrunch = 1;
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
	} else if (strcmp(conf_opt, "verylowmem") == 0) {
		if ((bsdtar->mode == 'c') && (bsdtar->cachecrunch == 0))
			bsdtar->cachecrunch = 2;
	} else {
		bsdtar_errc(bsdtar, 1, 0,
		    "Unrecognized configuration file option: \"%s\"",
		    conf_opt);
	}

	/* Free expanded argument or NULL. */
	free(conf_arg_malloced);

	/* Free memory allocated by strdup. */
	free(conf_opt);

	return (0);
}

/* Load keys from the specified file. */
static void
load_keys(struct bsdtar *bsdtar, const char *path)
{
	uint64_t machinenum;

	/* Load the key file. */
	if (crypto_keyfile_read(path, &machinenum))
		bsdtar_errc(bsdtar, 1, errno,
		    "Cannot read key file: %s", path);

	/* Check the machine number. */
	if ((bsdtar->machinenum != (uint64_t)(-1)) &&
	    (machinenum != bsdtar->machinenum))
		bsdtar_errc(bsdtar, 1, 0,
		    "Key file belongs to wrong machine: %s", path);
	bsdtar->machinenum = machinenum;
}
