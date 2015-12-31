#include <time.h>

#include "archive.h"

/**
 * archive_read_open_multitape(a, machinenum, tapename):
 * Open the multitape tape ${tapename} for reading (and skipping) and
 * associate it with the archive $a$.  Return a cookie which can be passed
 * to the multitape layer.
 */
void * archive_read_open_multitape(struct archive *, uint64_t, const char *);

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
void * archive_write_open_multitape(struct archive *, uint64_t, const char *,
    const char *, int argc, char ** argv, int, int, time_t, const char *);

/**
 * archive_write_multitape_setmode(a, cookie, mode):
 * Set the tape mode to 0 (HEADER), 1 (DATA), or 2 (finished archive entry).
 */
int archive_write_multitape_setmode(struct archive *, void *, int);

/**
 * archive_write_multitape_checkpoint(cookie):
 * Create a checkpoint in the archive associated with the write cookie
 * ${cookie}.
 */
int archive_write_multitape_checkpoint(void *);

/**
 * archive_write_multitape_truncate(cookie):
 * Record that the archive associated with the write cookie ${cookie}
 * should be truncated at the current position.
 */
void archive_write_multitape_truncate(void *);

/**
 * archive_multitape_copy(ina, read_cookie, a, write_cookie)
 * Copy the data for an entry from one archive to another.
 */
int archive_multitape_copy(struct archive *, void *, struct archive *,
    void *);
