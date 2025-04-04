/*-
 * Copyright 2006-2025 Tarsnap Backup Inc.
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

/*-
 * This is a new directory-walking system that addresses a number
 * of problems I've had with fts(3).  In particular, it has no
 * pathname-length limits (other than the size of 'int'), handles
 * deep logical traversals, uses considerably less memory, and has
 * an opaque interface (easier to modify in the future).
 *
 * Internally, it keeps a single list of "tree_entry" items that
 * represent filesystem objects that require further attention.
 * Non-directories are not kept in memory: they are pulled from
 * readdir(), returned to the client, then freed as soon as possible.
 * Any directory entry to be traversed gets pushed onto the stack.
 *
 * There is surprisingly little information that needs to be kept for
 * each item on the stack.  Just the name, depth (represented here as the
 * string length of the parent directory's pathname), and some markers
 * indicating how to get back to the parent (via chdir("..") for a
 * regular dir or via fchdir(2) for a symlink).
 */
#include "bsdtar_platform.h"
__FBSDID("$FreeBSD: src/usr.bin/tar/tree.c,v 1.9 2008/11/27 05:49:52 kientzle Exp $");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
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

#include "fileutil.h"

#include "tree.h"

/*
 * TODO:
 *    1) Loop checking.
 *    3) Arbitrary logical traversals by closing/reopening intermediate fds.
 */

struct tree_entry {
	struct tree_entry *next;
	char *name;
	size_t dirname_length;
	dev_t dev;
	ino_t ino;
#ifdef HAVE_FCHDIR
	int fd;
#elif defined(_WIN32) && !defined(__CYGWIN__)
	char *fullpath;
#else
#error fchdir function required.
#endif
	int flags;
};

/* Definitions for tree_entry.flags bitmap. */
#define	isDir 1 /* This entry is a regular directory. */
#define	isDirLink 2 /* This entry is a symbolic link to a directory. */
#define	needsPreVisit 4 /* This entry needs to be previsited. */
#define	needsPostVisit 8 /* This entry needs to be postvisited. */

/*
 * Local data for this package.
 */
struct tree {
	struct tree_entry	*stack;
	DIR	*d;
#ifdef HAVE_FCHDIR
	int	 initialDirFd;
#elif defined(_WIN32) && !defined(__CYGWIN__)
	char	*initialDir;
#endif
	int	 flags;
	int	 visit_type;
	int	 tree_errno; /* Error code from last failed operation. */

	char	*buff;
	const char	*basename;
	size_t	 buff_length;
	size_t	 path_length;
	size_t	 dirname_length;

	char	 realpath[PATH_MAX + 1];
	size_t	 realpath_dirname_length;
	int	 realpath_valid;
	char	 realpath_symlink[PATH_MAX + 1];

	int	 depth;
	int	 openCount;
	int	 maxOpenCount;

	int	 noatime;

	struct stat	lst;
	struct stat	st;
};

/* Definitions for tree.flags bitmap. */
#define needsReturn 8  /* Marks first entry as not having been returned yet. */
#define hasStat 16  /* The st entry is set. */
#define hasLstat 32 /* The lst entry is set. */


#ifdef HAVE_DIRENT_D_NAMLEN
/* BSD extension; avoids need for a strlen() call. */
#define D_NAMELEN(dp)	(dp)->d_namlen
#else
#define D_NAMELEN(dp)	(strlen((dp)->d_name))
#endif

static void
errmsg(const char *m)
{
	size_t s = strlen(m);
	ssize_t written;

	while (s > 0) {
		written = write(2, m, strlen(m));
		if (written <= 0)
			return;
		m += written;
		s -= written;
	}
}

/*
 * Attempt to opendir() with O_NOATIME if requested.  This is not supported by
 * all operating systems or filesystems.  If any error occurs, do not print any
 * message, and opendir() without O_NOATIME.
 */
static DIR*
tree_opendir(const char *path, int noatime)
{
#ifndef HAVE_FDOPENDIR

	(void)noatime; /* UNUSED */

	return (opendir(path));
#else
	const int flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
	DIR *dir;
	int fd;
	int saved_errno;

	/* Open a fd with noatime (if applicable). */
	if ((fd = fileutil_open_noatime(path, flags, noatime)) < 0)
		goto err0;

	/* Re-open as a DIR*. */
	if ((dir = fdopendir(fd)) == NULL)
		goto err1;

	/* Success! */
	return (dir);

err1:
	saved_errno = errno;
	close(fd);
	errno = saved_errno;
err0:
	/* Failure! */
	return (NULL);
#endif
}

/*
 * Add a directory path to the current stack.
 */
static void
tree_push(struct tree *t, const char *path)
{
	struct tree_entry *te;

	te = malloc(sizeof(*te));
	if (te == NULL)
		abort();
	memset(te, 0, sizeof(*te));
	te->next = t->stack;
	t->stack = te;
#ifdef HAVE_FCHDIR
	te->fd = -1;
#elif defined(_WIN32) && !defined(__CYGWIN__)
	te->fullpath = NULL;
#endif
	te->name = strdup(path);
	te->flags = needsPreVisit | needsPostVisit;
	te->dirname_length = t->dirname_length;
}

/*
 * Append a name to the current path.
 */
static void
tree_append(struct tree *t, const char *name, size_t name_length)
{
	char *p;
	size_t size_needed;

	if (t->buff != NULL)
		t->buff[t->dirname_length] = '\0';
	/* Strip trailing '/' from name, unless entire name is "/". */
	while (name_length > 1 && name[name_length - 1] == '/')
		name_length--;

	/* Resize pathname buffer as needed. */
	size_needed = name_length + 1 + t->dirname_length + 1;
	if (t->buff_length < size_needed) {
		if (t->buff_length < 1024)
			t->buff_length = 1024;
		while (t->buff_length < size_needed)
			t->buff_length *= 2;
		t->buff = realloc(t->buff, t->buff_length);
	}
	if (t->buff == NULL)
		abort();
	p = t->buff + t->dirname_length;
	t->path_length = t->dirname_length + name_length;
	/* Add a separating '/' if it's needed. */
	if (t->dirname_length > 0 && p[-1] != '/') {
		*p++ = '/';
		t->path_length ++;
	}
	strncpy(p, name, name_length);
	p[name_length] = '\0';
	t->basename = p;

	/* Adjust canonical name. */
	if ((t->realpath_valid) &&
	    (t->realpath_dirname_length + name_length + 1 <= PATH_MAX)) {
		t->realpath[t->realpath_dirname_length] = '/';
		memcpy(t->realpath + t->realpath_dirname_length  + 1,
		    name, name_length);
		t->realpath[t->realpath_dirname_length + name_length + 1] = 0;
	} else {
		t->realpath_valid = 0;
	}
}

/*
 * Open a directory tree for traversal.
 */
struct tree *
tree_open(const char *path, int noatime)
{
	struct tree *t;

	t = malloc(sizeof(*t));
	if (t == NULL)
		abort();
	memset(t, 0, sizeof(*t));
	t->noatime = noatime;
	t->stack = NULL;
	t->d = NULL;
	t->buff = NULL;
	tree_append(t, path, strlen(path));
#ifdef HAVE_FCHDIR
	t->initialDirFd = open(".", O_RDONLY);
#elif defined(_WIN32) && !defined(__CYGWIN__)
	t->initialDir = getcwd(NULL, 0);
#endif
	/*
	 * During most of the traversal, items are set up and then
	 * returned immediately from tree_next().  That doesn't work
	 * for the very first entry, so we set a flag for this special
	 * case.
	 */
	t->flags = needsReturn;
	return (t);
}

/*
 * We've finished a directory; ascend back to the parent.
 */
static int
tree_ascend(struct tree *t)
{
	struct tree_entry *te;
	int r = 0;

	te = t->stack;
	t->depth--;
	if (te->flags & isDirLink) {
#ifdef HAVE_FCHDIR
		if (fchdir(te->fd) != 0) {
			t->tree_errno = errno;
			r = TREE_ERROR_FATAL;
		}
		close(te->fd);
#elif defined(_WIN32) && !defined(__CYGWIN__)
		if (chdir(te->fullpath) != 0) {
			t->tree_errno = errno;
			r = TREE_ERROR_FATAL;
		}
		free(te->fullpath);
		te->fullpath = NULL;
#endif
		t->openCount--;
	} else {
		if (chdir("..") != 0) {
			t->tree_errno = errno;
			r = TREE_ERROR_FATAL;
		}
	}

	/* Figure out where we are. */
	if (getcwd(t->realpath, PATH_MAX) != NULL) {
		t->realpath_dirname_length = strlen(t->realpath);
		if (t->realpath[0] == '/' && t->realpath[1] == '\0')
			t->realpath_dirname_length = 0;
		t->realpath_valid = 1;
	} else {
		t->realpath_valid = 0;
	}

	return (r);
}

/*
 * Pop the working stack.
 */
static void
tree_pop(struct tree *t)
{
	struct tree_entry *te;

	t->buff[t->dirname_length] = '\0';
	te = t->stack;
	t->stack = te->next;
	t->dirname_length = te->dirname_length;
	t->basename = t->buff + t->dirname_length;
	/* Special case: starting dir doesn't skip leading '/'. */
	if (t->dirname_length > 0)
		t->basename++;
	free(te->name);
	free(te);
}

/*
 * Get the next item in the tree traversal.
 */
int
tree_next(struct tree *t)
{
	struct dirent *de = NULL;
	int r;

	/* If we're called again after a fatal error, that's an API
	 * violation.  Just crash now. */
	if (t->visit_type == TREE_ERROR_FATAL) {
		const char *msg = "Unable to continue traversing"
		    " directory hierarchy after a fatal error.\n";
		errmsg(msg);
		*(volatile int *)0 = 1; /* Deliberate SEGV; NULL pointer dereference. */
		exit(1); /* In case the SEGV didn't work. */
	}

	/* Handle the startup case by returning the initial entry. */
	if (t->flags & needsReturn) {
		t->flags &= ~needsReturn;
		return (t->visit_type = TREE_REGULAR);
	}

	while (t->stack != NULL) {
		/* If there's an open dir, get the next entry from there. */
		while (t->d != NULL) {
			errno = 0;
			de = readdir(t->d);
			if (de == NULL) {
				if (errno) {
					/* If readdir fails, we're screwed. */
					t->tree_errno = errno;
					closedir(t->d);
					t->d = NULL;
					t->visit_type = TREE_ERROR_FATAL;
					return (t->visit_type);
				}
				/* Reached end of directory. */
				closedir(t->d);
				t->d = NULL;
			} else if (de->d_name[0] == '.'
			    && de->d_name[1] == '\0') {
				/* Skip '.' */
			} else if (de->d_name[0] == '.'
			    && de->d_name[1] == '.'
			    && de->d_name[2] == '\0') {
				/* Skip '..' */
			} else {
				/*
				 * Append the path to the current path
				 * and return it.
				 */
				tree_append(t, de->d_name, D_NAMELEN(de));
				t->flags &= ~hasLstat;
				t->flags &= ~hasStat;
				return (t->visit_type = TREE_REGULAR);
			}
		}

		/* If the current dir needs to be visited, set it up. */
		if (t->stack->flags & needsPreVisit) {
			tree_append(t, t->stack->name, strlen(t->stack->name));
			t->stack->flags &= ~needsPreVisit;
			/* If it is a link, set up fd for the ascent. */
			if (t->stack->flags & isDirLink) {
#ifdef HAVE_FCHDIR
				t->stack->fd = open(".", O_RDONLY);
#elif defined(_WIN32) && !defined(__CYGWIN__)
				t->stack->fullpath = getcwd(NULL, 0);
#endif
				t->openCount++;
				if (t->openCount > t->maxOpenCount)
					t->maxOpenCount = t->openCount;
			}
			t->dirname_length = t->path_length;
			if (chdir(t->stack->name) != 0) {
				/* chdir() failed; return error */
				t->tree_errno = errno;
				tree_pop(t);
				return (t->visit_type = TREE_ERROR_DIR);
			}
			t->depth++;
			t->d = tree_opendir(".", t->noatime);
			if (t->d == NULL) {
				t->tree_errno = errno;
				r = tree_ascend(t); /* Undo "chdir" */
				tree_pop(t);
				t->visit_type = r != 0 ? r : TREE_ERROR_DIR;
				return (t->visit_type);
			}
			t->flags &= ~hasLstat;
			t->flags &= ~hasStat;
			t->basename = ".";

			/* Figure out where we are. */
			if (getcwd(t->realpath, PATH_MAX) != NULL) {
				t->realpath_dirname_length =
				    strlen(t->realpath);
				if (t->realpath[0] == '/' &&
				    t->realpath[1] == '\0')
					t->realpath_dirname_length = 0;
				t->realpath_valid = 1;
			} else {
				t->realpath_valid = 0;
			}

			return (t->visit_type = TREE_POSTDESCENT);
		}

		/* We've done everything necessary for the top stack entry. */
		if (t->stack->flags & needsPostVisit) {
			r = tree_ascend(t);
			tree_pop(t);
			t->flags &= ~hasLstat;
			t->flags &= ~hasStat;
			t->visit_type = r != 0 ? r : TREE_POSTASCENT;
			return (t->visit_type);
		}
	}
	return (t->visit_type = 0);
}

/*
 * Return error code.
 */
int
tree_errno(struct tree *t)
{
	return (t->tree_errno);
}

/*
 * Called by the client to mark the directory just returned from
 * tree_next() as needing to be visited.
 */
void
tree_descend(struct tree *t)
{
	if (t->visit_type != TREE_REGULAR)
		return;

	if (tree_current_is_physical_dir(t)) {
		tree_push(t, t->basename);
		t->stack->flags |= isDir;
	} else if (tree_current_is_dir(t)) {
		tree_push(t, t->basename);
		t->stack->flags |= isDirLink;
	}
}

/*
 * Get the stat() data for the entry just returned from tree_next().
 */
const struct stat *
tree_current_stat(struct tree *t)
{
	if (!(t->flags & hasStat)) {
		if (stat(t->basename, &t->st) != 0)
			return NULL;
		t->flags |= hasStat;
	}
	return (&t->st);
}

/*
 * Get the lstat() data for the entry just returned from tree_next().
 */
const struct stat *
tree_current_lstat(struct tree *t)
{
	if (!(t->flags & hasLstat)) {
		if (lstat(t->basename, &t->lst) != 0)
			return NULL;
		t->flags |= hasLstat;
	}
	return (&t->lst);
}

/*
 * Test whether current entry is a dir or link to a dir.
 */
int
tree_current_is_dir(struct tree *t)
{
	const struct stat *st;

	/*
	 * If we already have lstat() info, then try some
	 * cheap tests to determine if this is a dir.
	 */
	if (t->flags & hasLstat) {
		/* If lstat() says it's a dir, it must be a dir. */
		if (S_ISDIR(tree_current_lstat(t)->st_mode))
			return 1;
		/* Not a dir; might be a link to a dir. */
		/* If it's not a link, then it's not a link to a dir. */
		if (!S_ISLNK(tree_current_lstat(t)->st_mode))
			return 0;
		/*
		 * It's a link, but we don't know what it's a link to,
		 * so we'll have to use stat().
		 */
	}

	st = tree_current_stat(t);
	/* If we can't stat it, it's not a dir. */
	if (st == NULL)
		return 0;
	/* Use the definitive test.  Hopefully this is cached. */
	return (S_ISDIR(st->st_mode));
}

/*
 * Test whether current entry is a physical directory.  Usually, we
 * already have at least one of stat() or lstat() in memory, so we
 * use tricks to try to avoid an extra trip to the disk.
 */
int
tree_current_is_physical_dir(struct tree *t)
{
	const struct stat *st;

	/*
	 * If stat() says it isn't a dir, then it's not a dir.
	 * If stat() data is cached, this check is free, so do it first.
	 */
	if ((t->flags & hasStat)
	    && (!S_ISDIR(tree_current_stat(t)->st_mode)))
		return 0;

	/*
	 * Either stat() said it was a dir (in which case, we have
	 * to determine whether it's really a link to a dir) or
	 * stat() info wasn't available.  So we use lstat(), which
	 * hopefully is already cached.
	 */

	st = tree_current_lstat(t);
	/* If we can't stat it, it's not a dir. */
	if (st == NULL)
		return 0;
	/* Use the definitive test.  Hopefully this is cached. */
	return (S_ISDIR(st->st_mode));
}

/*
 * Test whether current entry is a symbolic link.
 */
int
tree_current_is_physical_link(struct tree *t)
{
	const struct stat *st = tree_current_lstat(t);
	if (st == NULL)
		return 0;
	return (S_ISLNK(st->st_mode));
}

/*
 * Return the access path for the entry just returned from tree_next().
 */
const char *
tree_current_access_path(struct tree *t)
{
	return (t->basename);
}

/*
 * Return the full path for the entry just returned from tree_next().
 */
const char *
tree_current_path(struct tree *t)
{
	return (t->buff);
}

/*
 * Return what you would get by calling realpath(3) on the path returned
 * by tree_current_access_path(t).  In most cases this avoids needing to
 * call realpath(3).
 */
const char *
tree_current_realpath(struct tree *t)
{

	if (tree_current_is_physical_link(t))
		return (realpath(t->basename, t->realpath_symlink));
	else if (t->realpath_valid)
		return (t->realpath);
	else
		return (realpath(t->basename, t->realpath));
}

/*
 * Return the length of the path for the entry just returned from tree_next().
 */
size_t
tree_current_pathlen(struct tree *t)
{
	return (t->path_length);
}

/*
 * Return the nesting depth of the entry just returned from tree_next().
 */
int
tree_current_depth(struct tree *t)
{
	return (t->depth);
}

/*
 * Terminate the traversal and release any resources.
 */
int
tree_close(struct tree *t)
{
	int rc = 0;

	/* Release anything remaining in the stack. */
	while (t->stack != NULL)
		tree_pop(t);
	if (t->buff)
		free(t->buff);
	/* chdir() back to where we started. */
#ifdef HAVE_FCHDIR
	if (t->initialDirFd >= 0) {
		rc = fchdir(t->initialDirFd);
		close(t->initialDirFd);
		t->initialDirFd = -1;
	}
#elif defined(_WIN32) && !defined(__CYGWIN__)
	if (t->initialDir != NULL) {
		rc = chdir(t->initialDir);
		free(t->initialDir);
		t->initialDir = NULL;
	}
#endif
	free(t);

	return (rc);
}
