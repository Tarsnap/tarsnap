#include "bsdtar_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	TAPE_D * d;
	size_t i;

	/* Prepare for deletes. */
	if ((d = deletetape_init(bsdtar->machinenum)) == NULL)
		goto err1;

	/* Delete archives. */
	for (i = 0; i < bsdtar->ntapes; i++) {
		if (bsdtar->verbose && (bsdtar->ntapes > 1))
			fprintf(stderr, "Deleting archive \"%s\"\n",
			    bsdtar->tapenames[i]);
		if (deletetape(d, bsdtar->machinenum, bsdtar->cachedir,
		    bsdtar->tapenames[i], bsdtar->option_print_stats,
		    bsdtar->ntapes > 1 ? 1 : 0))
			goto err1;
	}

	/* We've finished deleting archives. */
	deletetape_free(d);

	/* Success! */
	return;

err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error deleting archive");
	exit(1);
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
	    bsdtar->tapenames[0])) == NULL)
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
	size_t i;

	/* Open the archive set for statistics purposes. */
	if ((d = statstape_open(bsdtar->machinenum,
	    bsdtar->cachedir)) == NULL)
		goto err1;

	/* Print statistics about the archive set. */
	if (statstape_printglobal(d))
		goto err2;

	if (bsdtar->ntapes == 0) {
		/* User only wanted global statistics. */
	} else if ((bsdtar->tapenames[0][0] == '*') &&
	    (bsdtar->tapenames[0][1] == '\0')) {
		/* User wants statistics on all archives. */
		if (statstape_printall(d))
			goto err2;
	} else {
		/* User wants statistics about specific archive(s). */
		for (i = 0; i < bsdtar->ntapes; i++) {
			if (statstape_print(d, bsdtar->tapenames[i]))
				goto err2;
		}
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
tarsnap_mode_fsck(struct bsdtar *bsdtar, int prune, int whichkey)
{

	if (fscktape(bsdtar->machinenum, bsdtar->cachedir, prune, whichkey))
		goto err1;

	/* Success! */
	return;

err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error fscking archives");
	exit(1);
}

/*
 * Nuke all the files belonging to an archive set.
 */
void
tarsnap_mode_nuke(struct bsdtar *bsdtar)
{
	char s[100];

	/* Safeguard against being called accidentally. */
	fprintf(stderr, "Please type 'No Tomorrow' to continue\n");
	if (fgets(s, 100, stdin) == NULL) {
		bsdtar_warnc(bsdtar, 0,
		    "Error reading string from standard input");
		exit(1);
	}
	if (strcmp(s, "No Tomorrow\n")) {
		bsdtar_warnc(bsdtar, 0, "You didn't type 'No Tomorrow'");
		exit(1);
	}

	if (nuketape(bsdtar->machinenum))
		goto err1;

	/* Success! */
	return;

err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error nuking archives");
	exit(1);
}

/*
 * Recover an interrupted archive if one exists.
 */
void
tarsnap_mode_recover(struct bsdtar *bsdtar, int whichkey)
{

	if (recovertape(bsdtar->machinenum, bsdtar->cachedir, whichkey))
		goto err1;

	/* Success! */
	return;

err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error recovering archive");
	exit(1);
}
