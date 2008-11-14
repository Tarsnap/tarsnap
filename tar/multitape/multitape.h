#ifndef _MULTITAPE_H_
#define _MULTITAPE_H_

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

typedef struct multitape_read_internal TAPE_R;
typedef struct multitape_write_internal TAPE_W;
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
 *     dryrun):
 * Create a tape with the given name, and return a cookie which can be used
 * for accessing it.  The argument vector must be long-lived.
 */
TAPE_W * writetape_open(uint64_t, const char *, const char *, int, char **,
    int, int);

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
 * Return the length of the chunk if successful; 0 if the chunk has not been
 * stored previously; and -1 if an error occurs.
 * This function MUST NOT be called after a call to writetape_write unless
 * there is an intervening change of the tape mode.  This function MUST NOT
 * be called when the tape is in mode 0 (HEADER).
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
 * deletetape(machinenum, cachedir, tapename, printstats):
 * Delete the specified tape, and print statistics to stderr if requested.
 */
int deletetape(uint64_t, const char *, const char *, int);

/**
 * statstape_open(machinenum, cachedir):
 * Open the archive set in preparation for calls to _printglobal, _printall,
 * and _print.
 */
TAPE_S * statstape_open(uint64_t, const char *);

/**
 * statstape_printglobal(d):
 * Print global statistics relating to a set of archives.
 */
int statstape_printglobal(TAPE_S *);

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
 * Print statistics relating to a specific archive in a set.
 */
int statstape_print(TAPE_S *, const char *);

/**
 * statstape_close(d):
 * Close the given archive set.
 */
int statstape_close(TAPE_S *);

/**
 * fscktape(machinenum, cachedir):
 * Correct any inconsistencies in the archive set (by removing orphaned or
 * corrupt files) and reconstruct the chunk directory in ${cachedir}.
 */
int fscktape(uint64_t, const char *);

/**
 * nuketape(machinenum):
 * Delete all files in the archive set.
 */
int nuketape(uint64_t);

#endif /* !_MULTITAPE_H_ */
