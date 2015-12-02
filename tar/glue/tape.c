#include "bsdtar_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "archive.h"
#include "ccache.h"
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
		switch (deletetape(d, bsdtar->machinenum, bsdtar->cachedir,
		    bsdtar->tapenames[i], bsdtar->option_print_stats,
		    bsdtar->ntapes > 1 ? 1 : 0)) {
		case 0:
			break;
		case 1:
			if (bsdtar->option_keep_going)
				break;
			/* FALLTHROUGH */
		default:
			goto err2;
		}
	}

	/* We've finished deleting archives. */
	deletetape_free(d);

	/* Success! */
	return;

err2:
	deletetape_free(d);
err1:
	/* Failure! */
	bsdtar_warnc(bsdtar, 0, "Error deleting archive");
	bsdtar->return_value = 1;
	return;
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
	bsdtar->return_value = 1;
	return;
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
	if (statstape_printglobal(d, NULL))
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
			switch (statstape_print(d, bsdtar->tapenames[i])) {
			case 0:
				break;
			case 1:
				if (bsdtar->option_keep_going)
					break;
				/* FALLTHROUGH */
			default:
				goto err2;
			}
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
	bsdtar->return_value = 1;
	return;
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
	bsdtar->return_value = 1;
	return;
}

/*
 * Archive set consistency check and repair.
 */
void
tarsnap_mode_fsck(struct bsdtar *bsdtar, int prune, int whichkey)
{

	if (fscktape(bsdtar->machinenum, bsdtar->cachedir, prune, whichkey)) {
		bsdtar_warnc(bsdtar, 0, "Error fscking archives");
		goto err0;
	}

	/*
	 * Remove the chunkification cache in case whatever caused the fsck to
	 * be necessary (e.g., disk corruption) also damaged that cache.  The
	 * chunkification cache is purely a performance optimization; since
	 * we're dealing with backups here it makes sense to sacrifice some
	 * performance to prevent possible data loss.
	 */
	if (ccache_remove(bsdtar->cachedir)) {
		bsdtar_warnc(bsdtar, 0, "Error removing chunkification cache");
		goto err0;
	}

	/* Success! */
	return;

err0:
	/* Failure! */
	bsdtar->return_value = 1;
	return;
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
		goto err0;
	}
	if (strcmp(s, "No Tomorrow\n")) {
		bsdtar_warnc(bsdtar, 0, "You didn't type 'No Tomorrow'");
		goto err0;
	}

	if (nuketape(bsdtar->machinenum)) {
		bsdtar_warnc(bsdtar, 0, "Error nuking archives");
		goto err0;
	}

	/* Success! */
	return;

err0:
	/* Failure! */
	bsdtar->return_value = 1;
	return;
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
	bsdtar->return_value = 1;
	return;
}
