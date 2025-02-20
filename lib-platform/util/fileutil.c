#include "platform.h"

#include <fcntl.h>

#include "fileutil.h"

/**
 * fileutil_open_noatime(path, flags, noatime):
 * Act the same as open(2), except that if the OS supports O_NOATIME and
 * ${noatime} is non-zero, attempt to open the path with that set.  If the
 * O_NOATIME attempt fails, do not give any warnings, and attempt a normal
 * open().
 */
int
fileutil_open_noatime(const char * path, int flags, int noatime)
{
	int fd = -1;

#if defined(O_NOATIME)
	/* If requested, attempt to open with O_NOATIME. */
	if (noatime)
		fd = open(path, flags | O_NOATIME);
#else
	(void)noatime; /* UNUSED */
#endif

	/* If it failed (or wasn't supported / requested), open normally. */
	if ((fd == -1) && ((fd = open(path, flags)) == -1))
		goto err0;

	/* Success! */
	return (fd);

err0:
	/* Failure! */
	return (fd);
}
