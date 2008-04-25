#include "bsdtar_platform.h"

#include <stdlib.h>
#include <unistd.h>

#include "archive.h"
#include "multitape.h"

#include "bsdtar.h"

/*
 * Delete a tape.
 */
void
tarsnap_mode_d(struct bsdtar *bsdtar)
{

	if (deletetape(bsdtar->machinenum, bsdtar->cachedir,
	    bsdtar->tapename, bsdtar->option_print_stats)) {
		bsdtar_warnc(bsdtar, 0, "Error deleting archive");
		exit(1);
	}
}

/*
 * Read the tape and write to stdout.
 */
void
tarsnap_mode_r(struct bsdtar *bsdtar)
{
	TAPE_R * d;
	const void * buf;
	ssize_t lenread;
	size_t writelen;

	/* Open the tape. */
	if ((d = readtape_open(bsdtar->machinenum,
	    bsdtar->tapename)) == NULL)
		goto err1;

	/* Loop until we have an error or EOF. */
	do {
		lenread = readtape_read(d, &buf);

		/* Error? */
		if (lenread < 0)
			goto err2;

		/* EOF? */
		if (lenread == 0)
			break;

		/* Output data to stdout. */
		writelen = (size_t)(lenread);
		if (fwrite(buf, 1, writelen, stdout) != writelen)
			goto err2;
	} while (1);

	/* We're done!  Close the tape. */
	if (readtape_close(d))
		goto err1;

	/* Success! */
	return;

err2:
	readtape_close(d);

err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error reading archive");
	exit(1);
}

/*
 * Print statistics relating to an archive or set of archives.
 */
void
tarsnap_mode_print_stats(struct bsdtar *bsdtar)
{
	TAPE_S * d;

	/* Open the archive set for statistics purposes. */
	if ((d = statstape_open(bsdtar->machinenum,
	    bsdtar->cachedir)) == NULL)
		goto err1;

	/* Print statistics about the archive set. */
	if (statstape_printglobal(d))
		goto err2;

	if (bsdtar->tapename == NULL) {
		/* User only wanted global statistics. */
	} else if ((bsdtar->tapename[0] == '*') &&
	    (bsdtar->tapename[1] == '\0')) {
		/* User wants statistics on all archives. */
		if (statstape_printall(d))
			goto err2;
	} else {
		/* User wants statistics about a single archive. */
		if (statstape_print(d, bsdtar->tapename))
			goto err2;
	}

	/* We're done.  Close the archive set. */
	if (statstape_close(d))
		goto err1;

	/* Success! */
	return;

err2:
	statstape_close(d);
err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error generating archive statistics");
	exit(1);
}

/*
 * Print the names of all the archives.
 */
void
tarsnap_mode_list_archives(struct bsdtar *bsdtar)
{
	TAPE_S * d;

	/* Open the archive set for statistics purposes. */
	if ((d = statstape_open(bsdtar->machinenum, NULL)) == NULL)
		goto err1;

	/* Ask for the list of archives to be printed. */
	if (statstape_printlist(d, bsdtar->verbose))
		goto err2;

	/* We're done.  Close the archive set. */
	if (statstape_close(d))
		goto err1;

	/* Success! */
	return;

err2:
	statstape_close(d);
err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error listing archives");
	exit(1);
}

/*
 * Archive set consistency check and repair.
 */
void
tarsnap_mode_fsck(struct bsdtar *bsdtar)
{

	if (fscktape(bsdtar->machinenum, bsdtar->cachedir))
		goto err1;

	/* Success! */
	return;

err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error fscking archives");
	exit(1);
}
