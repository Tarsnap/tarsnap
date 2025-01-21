#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "warnp.h"

#include "dirutil.h"

/**
 * XXX Portability
 * XXX This function should ensure that in the sequence of events
 * XXX 1. Creation/link/unlink of a file in/to/from the directory X,
 * XXX 2. dirutil_fsyncdir(X),
 * XXX 3. Creation/link/unlink of a file anywhere else,
 * XXX the system can never (even in the event of power failure) have step 3
 * XXX take place but not step 1.
 * XXX
 * XXX Calling fsync on the directory X is reported to be sufficient to
 * XXX achieve this on all widely used systems (although not necessary on
 * XXX all of them), but this should be reviewed when porting this code.
 */
/**
 * dirutil_fsyncdir(path):
 * Call fsync on the directory ${path}.
 */
int
dirutil_fsyncdir(const char * path)
{
	int fd;

	/* Open the directory read-only. */
	if ((fd = open(path, O_RDONLY)) == -1) {
		warnp("open(%s)", path);
		return (-1);
	}

	/* Call fsync. */
	if (fsync(fd)) {
		warnp("fsync(%s)", path);
		if (close(fd))
			warnp("close");
		return (-1);
	}

	/* Close the descriptor. */
	if (close(fd)) {
		warnp("close(%s)", path);
		return (-1);
	}

	/* Success! */
	return (0);
}

/**
 * dirutil_fsync(fp, name):
 * Attempt to write the contents of ${fp} to disk.  Do not close ${fp}.
 *
 * Caveat: "Disks lie" - Kirk McKusick.
 */
int
dirutil_fsync(FILE * fp, const char * name)
{
	int fd;

	if (fflush(fp)) {
		warnp("fflush(%s)", name);
		goto err0;
	}
	if ((fd = fileno(fp)) == -1) {
		warnp("fileno(%s)", name);
		goto err0;
	}
	if (fsync(fd)) {
		warnp("fsync(%s)", name);
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * build_dir(dir, diropt):
 * Make sure that ${dir} exists, creating it (and any parents) as necessary.
 */
int
build_dir(const char * dir, const char * diropt)
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
		if (asprintf(&s, "%.*s", (int)(dirseppos - dir), dir) == -1) {
			warnp("No memory");
			goto err0;
		}

		/* Does the parent directory exist already? */
		if (stat(s, &sb) == 0)
			goto nextdir;

		/* Did something go wrong? */
		if (errno != ENOENT) {
			warnp("stat(%s)", s);
			goto err1;
		}

		/* Create the directory. */
		if (mkdir(s, 0700)) {
			warnp("Cannot create directory: %s", s);
			goto err1;
		}

		/* Tell the user what we did. */
		fprintf(stderr, "Directory %s created for \"%s %s\"\n",
		    s, diropt, dir);

nextdir:
		free(s);
	}

	/* Make sure permissions on the directory are correct. */
	if (stat(dir, &sb)) {
		warnp("stat(%s)", dir);
		goto err0;
	}
	if (sb.st_mode & (S_IRWXG | S_IRWXO)) {
		if (chmod(dir, sb.st_mode & (mode_t)(~(S_IRWXG | S_IRWXO)))) {
			warnp("Cannot sanitize permissions on directory: %s",
			    dir);
			goto err0;
		}
	}

	/* Success! */
	return (0);

err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}
