#ifndef _MULTITAPE_H_
#define _MULTITAPE_H_

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

typedef struct multitape_read_internal TAPE_R;
typedef struct multitape_write_internal TAPE_W;
typedef struct multitape_delete_internal TAPE_D;
typedef struct multitape_stats_internal TAPE_S;

struct chunkheader;

/**
 * readtape_open(machinenum, tapename):
 * Open the tape with the given name, and return a cookie which can be used
 * for accessing it.
 */
TAPE_R * readtape_open(uint64_t, const char *);

/**
 * readtape_read(d, buffer):
 * Read some data from the tape associated with ${d}, make *${buffer}
 * point to the data, and return the number of bytes read.
 */
ssize_t readtape_read(TAPE_R *, const void **);

/**
 * readtape_readchunk(d, ch):
 * Obtain a chunk header suitable for passing to writetape_writechunk.
 * Return the length of the chunk, or 0 if no chunk is available (EOF or if
 * the tape position isn't aligned at a chunk boundary).
 */
ssize_t readtape_readchunk(TAPE_R *, struct chunkheader **);

/**
 * readtape_skip(d, request):
 * Skip up to ${request} bytes from the tape associated with ${d},
 * and return the length skipped.
 */
off_t readtape_skip(TAPE_R *, off_t);

/**
 * readtape_close(d):
 * Close the tape associated with ${d}.
 */
int readtape_close(TAPE_R *);

/**
 * writetape_open(machinenum, cachedir, tapename, argc, argv, printstats,
 *     dryrun, creationtime):
 * Create a tape with the given name, and return a cookie which can be used
 * for accessing it.  The argument vector must be long-lived.
 */
TAPE_W * writetape_open(uint64_t, const char *, const char *, int, char **,
    int, int, time_t);

/**
 * writetape_setcallbacks(d, callback_chunk, callback_trailer,
 *     callback_cookie):
 * On the tape associated with ${d}, set ${callback_chunk} to be called
 * with the ${callback_cookie} parameter whenever a chunk header is written
 * which corresponds to data provided via writetape_write.  Set
 * ${callback_trailer} to be called whenever a trailer (i.e., file data which
 * is not in a chunk) is written.
 */
void writetape_setcallback(TAPE_W *,
    int (*)(void *, struct chunkheader *),
    int (*)(void *, const uint8_t *, size_t),
    void *);

/**
 * writetape_write(d, buffer, nbytes):
 * Write ${nbytes} bytes of data from ${buffer} to the tape associated with
 * ${d}.  Return ${nbytes} on success.
 */
ssize_t writetape_write(TAPE_W *, const void *, size_t);

/**
 * writetape_ischunkpresent(d, ch):
 * If the specified chunk exists, return its length; otherwise, return 0.
 */
ssize_t writetape_ischunkpresent(TAPE_W *, struct chunkheader *);

/**
 * writetape_writechunk(d, ch):
 * Attempt to add a (copy of a) pre-existing chunk to the tape being written.
 * Return the length of the chunk if successful; 0 if the chunk cannot be
 * added written via this interface but must instead be written using the
 * writetape_write interface (e.g., if the chunk does not exist or if the
 * tape is not in a state where a chunk can be written); or -1 if an error
 * occurs.
 */
ssize_t writetape_writechunk(TAPE_W *, struct chunkheader *);

/**
 * writetape_setmode(d, mode):
 * Set the tape mode to 0 (HEADER), 1 (DATA), or 2 (finished archive entry).
 */
int writetape_setmode(TAPE_W *, int);

/**
 * writetape_truncate(d):
 * Record that the archive is being truncated at the current position.
 */
void writetape_truncate(TAPE_W *);

/**
 * writetape_checkpoint(d):
 * Create a checkpoint in the tape associated with ${d}.
 */
int writetape_checkpoint(TAPE_W *);

/**
 * writetape_close(d):
 * Close the tape associated with ${d}.
 */
int writetape_close(TAPE_W *);

/**
 * writetape_free(d):
 * Free memory associated with ${d}; the archive is being cancelled.
 */
void writetape_free(TAPE_W *);

/**
 * deletetape_init(machinenum):
 * Return a cookie which can be passed to deletetape.
 */
TAPE_D * deletetape_init(uint64_t);

/**
 * deletetape(d, machinenum, cachedir, tapename, printstats, withname):
 * Delete the specified tape, and print statistics to stderr if requested.
 * If ${withname} is non-zero, print statistics with the archive name, not
 * just as "This archive".  Return 0 on success, 1 if the tape does not exist,
 * or -1 on other errors.
 */
int deletetape(TAPE_D *, uint64_t, const char *, const char *, int, int);

/**
 * deletetape_free(d):
 * Free the delete cookie ${d}.
 */
void deletetape_free(TAPE_D *);

/**
 * statstape_open(machinenum, cachedir):
 * Open the archive set in preparation for calls to _printglobal, _printall,
 * and _print.
 */
TAPE_S * statstape_open(uint64_t, const char *);

/**
 * statstape_printglobal(d, csv_filename):
 * Print global statistics relating to a set of archives.  If ${csv_filename}
 * is not NULL, output will be written in CSV format to that filename.
 */
int statstape_printglobal(TAPE_S *, const char *);

/**
 * statstape_printall(d):
 * Print statistics relating to each of the archives in a set.
 */
int statstape_printall(TAPE_S *);

/**
 * statstape_printlist(d, verbose):
 * Print the names of each of the archives in a set.  If verbose > 0, print
 * the creation times; if verbose > 1, print the argument vector of the
 * program invocation which created the archive.
 */
int statstape_printlist(TAPE_S *, int);

/**
 * statstape_print(d, tapename):
 * Print statistics relating to a specific archive in a set.  Return 0 on
 * success, 1 if the tape does not exist, or -1 on other errors.
 */
int statstape_print(TAPE_S *, const char *);

/**
 * statstape_close(d):
 * Close the given archive set.
 */
int statstape_close(TAPE_S *);

/**
 * fscktape(machinenum, cachedir, prune, whichkey):
 * Correct any inconsistencies in the archive set (by removing orphaned or
 * corrupt files) and reconstruct the chunk directory in ${cachedir}.  If
 * ${prune} is zero, don't correct inconsistencies; instead, exit with an
 * error.  If ${whichkey} is zero, use the write key (for non-pruning fsck
 * only); otherwise, use the delete key.
 */
int fscktape(uint64_t, const char *, int, int);

/**
 * recovertape(machinenum, cachedir, whichkey):
 * Complete any pending checkpoint or commit, including a checkpoint in a
 * write transaction being performed by a different machine (if any).  If
 * ${whichkey} is zero, use the write key; otherwise, use the delete key.
 */
int recovertape(uint64_t, const char *, int);

/**
 * nuketape(machinenum):
 * Delete all files in the archive set.
 */
int nuketape(uint64_t);

#endif /* !_MULTITAPE_H_ */
