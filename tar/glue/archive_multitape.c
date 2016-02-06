#include "bsdtar_platform.h"

#include <errno.h>

#include "archive.h"
#include "multitape.h"
#include "tsnetwork.h"
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
		archive_set_error(a, errno, "Error reading archive");
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
		archive_set_error(a, errno, "Error reading archive");
		return (ARCHIVE_FATAL);
	} else
		return (skiplen);
}

static int
read_close(struct archive * a, void * cookie)
{
	struct multitape_read_internal * d = cookie;

	if (readtape_close(d)) {
		archive_set_error(a, errno, "Error closing archive");
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
		archive_set_error(a, errno, "Error writing archive");
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
		archive_set_error(a, errno, "Error closing archive");
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

	/* Clear any error messages from the archive. */
	archive_clear_error(a);

	if ((d = readtape_open(machinenum, tapename)) == NULL) {
		archive_set_error(a, errno, "Error opening archive");
		return (NULL);
	}

	if (archive_read_open2(a, d, NULL, read_read, read_skip, read_close))
		return (NULL);
	else
		return (d);
}

/**
 * archive_write_open_multitape(a, machinenum, cachedir, tapename, argc,
 *     argv, printstats, dryrun, creationtime, csv_filename):
 * Open the multitape tape ${tapename} for writing and associate it with the
 * archive $a$.  If ${printstats} is non-zero, print archive statistics when
 * the tape is closed.  If ${dryrun} is non-zero, perform a dry run.
 * Record ${creationtime} as the creation time in the archive metadata.
 * If ${csv_filename} is given, write statistics in CSV format.
 * Return a cookie which can be passed to the multitape layer.
 */
void *
archive_write_open_multitape(struct archive * a, uint64_t machinenum,
    const char * cachedir, const char * tapename, int argc,
    char ** argv, int printstats, int dryrun, time_t creationtime,
    const char * csv_filename)
{
	struct multitape_write_internal * d;

	/* Clear any error messages from the archive. */
	archive_clear_error(a);

	if ((d = writetape_open(machinenum, cachedir, tapename,
	    argc, argv, printstats, dryrun, creationtime,
	    csv_filename)) == NULL) {
		archive_set_error(a, errno, "Error creating new archive");
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
		archive_set_error(a, errno, "Error writing archive");
		return (ARCHIVE_FATAL);
	} else
		return (ARCHIVE_OK);
}

/**
 * archive_write_multitape_checkpoint(cookie):
 * Create a checkpoint in the archive associated with the write cookie
 * ${cookie}.
 */
int
archive_write_multitape_checkpoint(void * cookie)
{
	struct multitape_write_internal * d = cookie;

	return (writetape_checkpoint(d));
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
int
archive_multitape_copy(struct archive * ina, void * read_cookie,
    struct archive * a, void * write_cookie)
{
	char	buff[64*1024];
	struct chunkheader * ch;
	ssize_t lenread;
	ssize_t writelen;
	off_t entrylen;
	ssize_t backloglen;

	/* Compute the entry size. */
	if ((entrylen = archive_read_get_entryleft(ina)) < 0) {
		archive_set_error(ina, ENOSYS,
		    "read_get_entryleft not supported");
		return (-2);
	}

	/* Copy data. */
	while (entrylen > 0) {
		/* Is there data buffered by libarchive? */
		if ((backloglen = archive_read_get_backlog(ina)) < 0) {
			warn0("Error reading libarchive data backlog");
			return (-2);
		}
		if (backloglen > 0) {
			/* Drain some data from libarchive. */
			if ((size_t)backloglen > sizeof(buff))
				lenread = sizeof(buff);
			else
				lenread = backloglen;
			lenread = archive_read_data(ina, buff, lenread);
			if (lenread == 0) {
				warn0("libarchive claims data backlog,"
				    " but no data can be read?");
				return (-2);
			}
			if (lenread < 0)
				return (-2);

			/* Write it out to the new archive. */
			writelen = archive_write_data(a, buff, lenread);
			if (writelen < lenread)
				return (-1);

			/* Adjust the remaining entry length and continue. */
			entrylen -= lenread;
			continue;
		}

		/* Attempt to read a chunk for fast-pathing. */
		lenread = readtape_readchunk(read_cookie, &ch);
		if (lenread < 0)
			return (-2);
		if (lenread > entrylen) {
			warn0("readchunk returned chunk beyond end"
			    " of archive entry?");
			return (-2);
		}
		if (lenread == 0)
			goto nochunk;

		/* Attempt to write the chunk via the fast path. */
		writelen = writetape_writechunk(write_cookie, ch);
		if (writelen < 0)
			return (-1);
		if (writelen == 0)
			goto nochunk;
		if (writelen != lenread) {
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
		continue;

nochunk:
		/*
		 * We have no data buffered in libarchive, and we can't copy
		 * an intact chunk.  We need to read some data, but we have
		 * no idea how much the multitape layer wants to provide to
		 * libarchive next; and we don't want to read too much data
		 * since we might waste time reading and writing chunked data
		 * which could be fast-pathed.  Simple solution: Read and
		 * write one byte.  Libarchive will almost certainly get more
		 * than one byte from the multitape layer, but when we return
		 * to the start of this loop and handle backlogged data we
		 * will pick up the rest of the data.  (Also, this is always
		 * where we end up when we hit the end of an archive entry,
		 * in which case archive_read_data returns 0 and we exit the
		 * loop.)
		 */
		lenread = archive_read_data(ina, buff, 1);
		if (lenread == 0)
			break;
		if (lenread < 0)
			return (-2);
		writelen = archive_write_data(a, buff, 1);
		if (writelen < 1)
			return (-1);
	};

	return (0);
}
