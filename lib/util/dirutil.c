#include "bsdtar_platform.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
		close(fd);
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
 * dirutil_needdir(dirname):
 * Make sure that ${dirname} exists (creating it if necessary) and is a
 * directory.
 */
int
dirutil_needdir(const char * dirname)
{
	struct stat sb;

	if (stat(dirname, &sb) == -1) {
		if (errno != ENOENT) {
			warnp("stat(%s)", dirname);
			return (-1);
		}

		/* Directory does not exist; try to create it. */
		if (mkdir(dirname, 0777)) {
			warnp("mkdir(%s)", dirname);
			return (-1);
		}
	} else {
		/* The path exists; is it a directory? */
		if (! S_ISDIR(sb.st_mode)) {
			warn0("%s is not a directory", dirname);
			return (-1);
		}
	}

	/* Success! */
	return (0);
}
