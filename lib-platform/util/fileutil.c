#include "platform.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "warnp.h"

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

/**
 * fileutil_fsync(fp, name):
 * Attempt to write the contents of ${fp} to disk.  Do not close ${fp}.
 *
 * Caveat: "Disks lie" - Kirk McKusick.
 */
int
fileutil_fsync(FILE * fp, const char * name)
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

#ifdef F_FULLFSYNC
	/*
	 * MacOS-specific "ask the drive to flush all buffered data".
	 * Not supported on all filesystems.  Even on supported filesystems,
	 * some FireWire drives are known to ignore this request.  As such,
	 * don't pay attention to the return code from fcntl().
	 */
	fcntl(fd, F_FULLFSYNC);
#endif

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
