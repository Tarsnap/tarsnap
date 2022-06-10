#include "platform.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "chunks.h"
#include "ctassert.h"
#include "multitape_internal.h"
#include "storage.h"
#include "sysendian.h"
#include "warnp.h"

#include "multitape.h"

/* Buffer length for printing dates. */
#define DATEBUFLEN 100

/*
 * "Cookie" structure created by statstape_open and passed to other functions.
 */
struct multitape_stats_internal {
	uint64_t machinenum;
	CHUNKS_S * C;
	STORAGE_R * SR;
};

static int callback_print(void *, struct chunkheader *);

/**
 * callback_print(cookie, ch):
 * Call chunks_stats_addchunk on the chunk stats cookie ${cookie} and the
 * chunk header ${ch}.
 */
static int
callback_print(void * cookie, struct chunkheader * ch)
{
	CHUNKS_S * C = cookie;
	size_t len, zlen;
	int rc;

	/* Decode chunk header. */
	len = le32dec(ch->len);
	zlen = le32dec(ch->zlen);
	if ((rc = chunks_stats_addchunk(C, ch->hash, len, zlen)) == 1) {
		warn0("Directory is not consistent with archive: Run --fsck");
		rc = -1;
	}

	/* Return status. */
	return (rc);
}

/**
 * statstape_open(machinenum, cachedir):
 * Open the archive set in preparation for calls to _printglobal, _printall,
 * and _print.
 */
TAPE_S *
statstape_open(uint64_t machinenum, const char * cachedir)
{
	struct multitape_stats_internal * d;

	/* Allocate memory. */
	if ((d = malloc(sizeof(struct multitape_stats_internal))) == NULL)
		goto err0;
	d->machinenum = machinenum;

	/* Obtain storage layer cookie. */
	if ((d->SR = storage_read_init(machinenum)) == NULL)
		goto err1;

	/* Obtain chunk layer cookies. */
	if (cachedir != NULL) {
		if ((d->C = chunks_stats_init(cachedir)) == NULL)
			goto err2;
	} else {
		d->C = NULL;
	}

	/* Success! */
	return (d);

err2:
	storage_read_free(d->SR);
err1:
	free(d);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * statstape_printglobal(d, csv_filename):
 * Print global statistics relating to a set of archives.  If ${csv_filename}
 * is not NULL, output will be written in CSV format to that filename.
 */
int
statstape_printglobal(TAPE_S * d, const char * csv_filename)
{
	FILE * output = stdout;
	int csv = 0;

	/* Should we output to a CSV file? */
	if (csv_filename != NULL)
		csv = 1;

	/* Open CSV output file, if requested. */
	if (csv && (output = fopen(csv_filename, "w")) == NULL)
		goto err0;

	/* Ask the chunk storage layer to do this. */
	if (chunks_stats_printglobal(output, d->C, csv))
		goto err1;

	/* Close CSV output file, if requested. */
	if (csv && fclose(output))
		goto err0;

	/* Success! */
	return (0);

err1:
	if (output != stdout)
		fclose(output);
err0:
	/* Failure! */
	return (-1);
}

/**
 * statstape_printall(d, csv_filename):
 * Print statistics relating to each of the archives in a set.  If
 * ${csv_filename} is not NULL, output will be written in CSV format to that
 * filename.
 */
int
statstape_printall(TAPE_S * d, const char * csv_filename)
{
	struct tapemetadata tmd;
	uint8_t * flist;
	size_t nfiles;
	size_t file;
	FILE * output = stdout;
	int csv = 0;

	/* Should we output to a CSV file? */
	if (csv_filename != NULL)
		csv = 1;

	/* Open CSV output file, if requested. */
	if (csv && (output = fopen(csv_filename, "a")) == NULL)
		goto err0;

	/* Get a list of the metadata files. */
	if (storage_directory_read(d->machinenum, 'm', 0, &flist, &nfiles))
		goto err1;

	/* Cache up to 100 bytes of blocks per chunk in the directory. */
	storage_read_set_cache_limit(d->SR, 100 * chunks_stats_getdirsz(d->C));

	/* Iterate through the files. */
	for (file = 0; file < nfiles; file++) {
		/* Zero archive statistics. */
		chunks_stats_zeroarchive(d->C);

		/* Read the tape metadata. */
		if (multitape_metadata_get_byhash(d->SR, d->C, &tmd,
		    &flist[file * 32], 0))
			goto err2;

		/* Compute statistics. */
		if (multitape_chunkiter_tmd(d->SR, d->C, &tmd,
		    callback_print, d->C, 0))
			goto err3;

		/* Print the statistics. */
		if (chunks_stats_printarchive(output, d->C, tmd.name, csv))
			goto err3;

		/* Free parsed metadata. */
		multitape_metadata_free(&tmd);
	}

	/* Free the list of files. */
	free(flist);

	/* Close CSV output file, if requested. */
	if (csv && fclose(output))
		goto err0;

	/* Success! */
	return (0);

err3:
	multitape_metadata_free(&tmd);
err2:
	free(flist);
err1:
	if (output != stdout)
		fclose(output);
err0:
	/* Failure! */
	return (-1);
}

/**
 * statstape_printlist(d, verbose):
 * Print the names of each of the archives in a set.  If ${verbose} > 0, print
 * the creation times; if ${verbose} > 1, print the argument vector of the
 * program invocation which created the archive.
 */
int
statstape_printlist(TAPE_S * d, int verbose)
{
	struct tapemetadata tmd;
	uint8_t * flist;
	size_t nfiles;
	size_t file;
	struct tm * ltime;
	char datebuf[DATEBUFLEN];
	int arg;

	/* Get a list of the metadata files. */
	if (storage_directory_read(d->machinenum, 'm', 0, &flist, &nfiles))
		goto err0;

	/* Iterate through the files. */
	for (file = 0; file < nfiles; file++) {
		/* Read the tape metadata. */
		if (multitape_metadata_get_byhash(d->SR, NULL, &tmd,
		    &flist[file * 32], 0))
			goto err1;

		/* Print name. */
		if (fprintf(stdout, "%s", tmd.name) < 0) {
			warnp("fprintf");
			goto err2;
		}

		/* Print creation time. */
		if (verbose > 0 && tmd.ctime != -1) {
			if ((ltime = localtime(&tmd.ctime)) == NULL) {
				warnp("localtime");
				goto err2;
			}
			if (strftime(datebuf, DATEBUFLEN, "%F %T",
			    ltime) == 0) {
				warnp("Cannot format date");
				goto err2;
			}
			if (fprintf(stdout, "\t%s", datebuf) < 0) {
				warnp("fprintf");
				goto err2;
			}
		}

		/* Print command line. */
		if (verbose > 1) {
			for (arg = 0; arg < tmd.argc; arg++) {
				if (fprintf(stdout, arg ? " %s" : "\t%s",
				    tmd.argv[arg]) < 0) {
					warnp("fprintf");
					goto err2;
				}
			}
		}

		/* End line. */
		if (fprintf(stdout, "\n") < 0) {
			warnp("fprintf");
			goto err2;
		}

		/* Free parsed metadata. */
		multitape_metadata_free(&tmd);
	}

	/* Free the list of files. */
	free(flist);

	/* Success! */
	return (0);

err2:
	multitape_metadata_free(&tmd);
err1:
	free(flist);
err0:
	/* Failure! */
	return (-1);
}

/**
 * statstape_print(d, tapename, csv_filename):
 * Print statistics relating to a specific archive in a set.  Return 0 on
 * success, 1 if the tape does not exist, or -1 on other errors.  If
 * ${csv_filename} is not NULL, output will be written in CSV format to that
 * filename.
 */
int
statstape_print(TAPE_S * d, const char * tapename, const char * csv_filename)
{
	struct tapemetadata tmd;
	int rc = -1;	/* Presume error was not !found. */
	FILE * output = stdout;
	int csv = 0;

	/* Should we output to a CSV file? */
	if (csv_filename != NULL)
		csv = 1;

	/* Cache up to 100 bytes of blocks per chunk in the directory. */
	storage_read_set_cache_limit(d->SR, 100 * chunks_stats_getdirsz(d->C));

	/* Zero archive statistics. */
	chunks_stats_zeroarchive(d->C);

	/* Read the tape metadata. */
	switch (multitape_metadata_get_byname(d->SR, d->C, &tmd, tapename, 0)) {
	case 0:
		break;
	case 1:
		rc = 1;
		/* FALLTHROUGH */
	default:
		goto err0;
	}

	if (multitape_chunkiter_tmd(d->SR, d->C, &tmd,
	    callback_print, d->C, 0))
		goto err2;

	/* Free parsed metadata. */
	multitape_metadata_free(&tmd);

	/* Open CSV output file, if requested. */
	if (csv && (output = fopen(csv_filename, "a")) == NULL)
		goto err0;

	/* Print the statistics. */
	if (chunks_stats_printarchive(output, d->C, tapename, csv))
		goto err1;

	/* Close CSV output file. */
	if (csv && fclose(output))
		goto err0;

	/* Success! */
	return (0);

err2:
	multitape_metadata_free(&tmd);
err1:
	if (output != stdout)
		fclose(output);
err0:
	/* Failure! */
	return (rc);
}

/**
 * statstape_close(d):
 * Close the given archive set.
 */
int
statstape_close(TAPE_S * d)
{

	/* Free chunk layer cookies. */
	chunks_stats_free(d->C);

	/* Free storage layer cookie. */
	storage_read_free(d->SR);

	/* Free multitape cookie. */
	free(d);

	/* Success! */
	return (0);
}
