#include "bsdtar_platform.h"

#include <errno.h>

#include "archive.h"
#include "multitape.h"
#include "network.h"
#include "warnp.h"

#include "archive_multitape.h"

static ssize_t	read_read(struct archive *, void *, const void **);
static off_t	read_skip(struct archive *, void *, off_t);
static int	read_close(struct archive *, void *);
static ssize_t	write_write(struct archive *, void *, const void *, size_t);
static int	write_close(struct archive *, void *);

static ssize_t
read_read(struct archive * a, void * cookie, const void ** buffer)
{
	struct multitape_read_internal * d = cookie;
	ssize_t lenread;

	lenread = readtape_read(d, buffer);
	if (lenread < 0) {
		archive_set_error(a, 0, "Error reading archive");
		return (ARCHIVE_FATAL);
	} else
		return (lenread);
}

static off_t
read_skip(struct archive * a, void * cookie, off_t request)
{
	struct multitape_read_internal * d = cookie;
	off_t skiplen;

	skiplen = readtape_skip(d, request);
	if (skiplen < 0) {
		archive_set_error(a, 0, "Error reading archive");
		return (ARCHIVE_FATAL);
	} else
		return (skiplen);
}

static int
read_close(struct archive * a, void * cookie)
{
	struct multitape_read_internal * d = cookie;

	if (readtape_close(d)) {
		archive_set_error(a, 0, "Error closing archive");
		return (ARCHIVE_FATAL);
	} else
		return (ARCHIVE_OK);
}

static ssize_t
write_write(struct archive * a, void * cookie, const void * buffer,
    size_t nbytes)
{
	struct multitape_write_internal * d = cookie;
	ssize_t writelen;

	writelen = writetape_write(d, buffer, nbytes);
	if (writelen < 0) {
		archive_set_error(a, 0, "Error writing archive");
		return (ARCHIVE_FATAL);
	} else if (writelen == 0) {
		archive_clear_error(a);
		archive_set_error(a, 0, "Archive truncated");
		return (ARCHIVE_WARN);
	} else
		return (nbytes);
}

static int
write_close(struct archive * a, void * cookie)
{
	struct multitape_write_internal * d = cookie;

	if (writetape_close(d)) {
		archive_set_error(a, 0, "Error closing archive");
		return (ARCHIVE_FATAL);
	} else
		return (ARCHIVE_OK);
}

/**
 * archive_read_open_multitape(a, machinenum, tapename):
 * Open the multitape tape ${tapename} for reading (and skipping) and
 * associate it with the archive $a$.  Return a cookie which can be passed
 * to the multitape layer.
 */
void *
archive_read_open_multitape(struct archive * a, uint64_t machinenum,
    const char * tapename)
{
	struct multitape_read_internal * d;

	if ((d = readtape_open(machinenum, tapename)) == NULL) {
		archive_set_error(a, 0, "Error opening archive");
		return (NULL);
	}

	if (archive_read_open2(a, d, NULL, read_read, read_skip, read_close))
		return (NULL);
	else
		return (d);
}

/**
 * archive_write_open_multitape(a, machinenum, cachedir, tapename, argc,
 *     argv, printstats):
 * Open the multitape tape ${tapename} for writing and associate it with the
 * archive $a$.  If ${printstats} is non-zero, print archive statistics when
 * the tape is closed.  Return a cookie which which can be passed to the
 * multitape layer.
 */
void *
archive_write_open_multitape(struct archive * a, uint64_t machinenum,
    const char * cachedir, const char * tapename, int argc,
    char ** argv, int printstats)
{
	struct multitape_write_internal * d;

	if ((d = writetape_open(machinenum, cachedir, tapename,
	    argc, argv, printstats)) == NULL) {
		archive_set_error(a, 0, "Error creating new archive");
		return (NULL);
	}

	if (archive_write_open(a, d, NULL, write_write, write_close)) {
		writetape_free(d);
		return (NULL);
	} else
		return (d);
}

/**
 * archive_write_multitape_setmode(a, cookie, mode):
 * Set the tape mode to 0 (HEADER) or 1 (DATA).
 */
int
archive_write_multitape_setmode(struct archive * a, void * cookie, int mode)
{
	struct multitape_write_internal * d = cookie;

	if (writetape_setmode(d, mode)) {
		archive_set_error(a, 0, "Error writing archive");
		return (ARCHIVE_FATAL);
	} else
		return (ARCHIVE_OK);
}

/**
 * archive_write_multitape_truncate(cookie):
 * Record that the archive associated with the write cookie ${cookie}
 * should be truncated at the current position.
 */
void
archive_write_multitape_truncate(void * cookie)
{
	struct multitape_write_internal * d = cookie;

	writetape_truncate(d);
}

/**
 * archive_multitape_copy(ina, read_cookie, a, write_cookie)
 * Copy the data for an entry from one archive to another.
 */
int archive_multitape_copy(struct archive * ina, void * read_cookie,
    struct archive * a, void * write_cookie)
{
	char	buff[64*1024];
	struct chunkheader * ch;
	ssize_t lenread;
	ssize_t writelen;
	off_t entrylen;

	/* Compute the entry size. */
	if ((entrylen = archive_read_get_entryleft(ina)) < 0) {
		archive_set_error(ina, ENOSYS,
		    "read_get_entryleft not supported");
		return (-2);
	}

	/* Make sure there isn't any backlogged data. */
	if (archive_read_get_backlog(ina)) {
		warn0("Cannot use @@ fast path: data is backlogged");
	} else {
		do {
			/* Read a chunk. */
			lenread = readtape_readchunk(read_cookie, &ch);
			if (lenread < 0)
				return (-2);
			if (lenread == 0)
				break;
			if (lenread > entrylen) {
				/* Should never happen. */
				warn0("readchunk returned chunk beyond end"
				    " of archive entry?");
				break;
			}

			/* Write the chunk. */
			writelen = writetape_writechunk(write_cookie, ch);
			if (writelen < 0)
				return (-1);
			if (writelen == 0) {
				/*
				 * Shouldn't happen; but we'll let the error
				 * be reported via archive_read_data later.
				 */
				break;
			}
			if (writelen != lenread) {
				/* Should never happen. */
				warn0("chunk write size != chunk read size?");
				return (-1);
			}

			/*
			 * Advance libarchive pointers.  Do the write pointer
			 * first since a failure there is fatal.
			 */
			if (archive_write_skip(a, writelen))
				return (-1);
			if (archive_read_advance(ina, lenread))
				return (-2);

			/* We don't need to see this chunk again. */
			if (readtape_skip(read_cookie, lenread) != lenread) {
				warn0("could not skip read data?");
				return (-2);
			}

			/* We've done part of the entry. */
			entrylen -= lenread;
		} while (1);
	}

	/* Copy any remaining data between archives. */
	do {
		lenread = archive_read_data(ina, buff, sizeof(buff));
		if (lenread == 0)
			break;
		if (lenread < 0)
			return (-2);

		writelen = archive_write_data(a, buff, lenread);
		if (writelen < lenread)
			return (-1);
	} while (1);

	return (0);
}
