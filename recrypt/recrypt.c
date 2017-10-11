#include "bsdtar_platform.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "crypto.h"
#include "dirutil.h"
#include "getopt.h"
#include "imalloc.h"
#include "keyfile.h"
#include "multitape_internal.h"
#include "storage.h"
#include "tarsnap_opt.h"
#include "tsnetwork.h"
#include "warnp.h"

/* Copy batches of 16384 blocks; print a . per 512 blocks. */
#define BATCHLEN 16384
#define BATCHDOT 512

/* Read blocks using 8 connections. */
#define NCONNS 8

/* Global tarsnap options declared in tarsnap_opt.h. */
int tarsnap_opt_aggressive_networking = 1;
int tarsnap_opt_noisy_warnings = 0;
int tarsnap_opt_humanize_numbers = 0;
uint64_t tarsnap_opt_checkpointbytes = (uint64_t)(-1);
uint64_t tarsnap_opt_maxbytesout = (uint64_t)(-1);

struct block {
	char class;
	uint8_t name[32];
};

struct reader {
	STORAGE_R * SR;
	int status;
	struct block * b;
	uint8_t * buf;
	size_t buflen;
};

static void
usage(void)
{

	fprintf(stderr, "usage: tarsnap-recrypt %s %s %s %s\n",
	    "--oldkey old-key-file", "--oldcachedir old-cache-dir",
	    "--newkey new-key-file", "--newcachedir new-cache-dir");
	fprintf(stderr, "       tarsnap-recrypt --version\n");
	exit(1);

	/* NOTREACHED */
}

static void
lockdirs(const char * odir, const char * ndir, int * odirlock, int * ndirlock)
{
	struct stat sbo, sbn;

	/* PEBKAC check: make sure the two paths are different. */
	if (strcmp(odir, ndir) == 0) {
		warn0("Old and new cache directories must be different");
		exit(1);
	}

	/* Lock the two cache directories. */
	if ((*odirlock = multitape_lock(odir)) == -1) {
		warnp("Cannot lock old cache directory: %s", odir);
		exit(1);
	}
	if ((*ndirlock = multitape_lock(ndir)) == -1) {
		warnp("Cannot lock new cache directory: %s", ndir);
		exit(1);
	}

	/* Make sure we didn't lock the same file twice. */
	if (fstat(*odirlock, &sbo) || fstat(*ndirlock, &sbn)) {
		warnp("Cannot stat lockfile");
		exit(1);
	}
	if ((sbn.st_dev == sbo.st_dev) && (sbn.st_ino == sbo.st_ino)) {
		warn0("Old and new cache directories must be different");
		exit(1);
	}
}

static void
getblist(uint64_t mnum, struct block ** blist, size_t * blistlen)
{
	uint8_t * flist_m;
	size_t nfiles_m;
	uint8_t * flist_i;
	size_t nfiles_i;
	uint8_t * flist_c;
	size_t nfiles_c;
	size_t i, j;

	/* Get lists of {metadata, metaindex, chunk} files. */
	if (storage_directory_read(mnum, 'm', 0, &flist_m, &nfiles_m)) {
		warnp("Error reading metadata file list");
		exit(1);
	}
	if (storage_directory_read(mnum, 'i', 0, &flist_i, &nfiles_i)) {
		warnp("Error reading metaindex file list");
		exit(1);
	}
	if (storage_directory_read(mnum, 'c', 0, &flist_c, &nfiles_c)) {
		warnp("Error reading chunk file list");
		exit(1);
	}

	/* If there are no blocks, we have nothing to do. */
	if (nfiles_m + nfiles_i + nfiles_c == 0) {
		*blistlen = 0;
		*blist = NULL;
		return;
	}

	/* Allocate array of blocks. */
	*blistlen = nfiles_m + nfiles_i + nfiles_c;
	if (IMALLOC(*blist, *blistlen, struct block)) {
		warnp("Cannot allocate memory");
		exit(1);
	}

	/* Copy block names into the array in lexicographical order. */
	for (i = 0, j = 0; i < nfiles_c; i++, j++) {
		(*blist)[j].class = 'c';
		memcpy((*blist)[j].name, &flist_c[i * 32], 32);
	}
	for (i = 0; i < nfiles_i; i++, j++) {
		(*blist)[j].class = 'i';
		memcpy((*blist)[j].name, &flist_i[i * 32], 32);
	}
	for (i = 0; i < nfiles_m; i++, j++) {
		(*blist)[j].class = 'm';
		memcpy((*blist)[j].name, &flist_m[i * 32], 32);
	}

	/* Free the individual arrays. */
	free(flist_c);
	free(flist_i);
	free(flist_m);
}

static int
cmpblock(const struct block * x, const struct block * y)
{

	if (x->class < y->class)
		return (-1);
	else if (x->class > y->class)
		return (1);
	else
		return (memcmp(x->name, y->name, 32));
}

static void
compareblists(const struct block * oblist, size_t oblistlen,
    const struct block * nblist, size_t nblistlen,
    struct block ** cblist, size_t * cblistlen)
{
	size_t i, j, k;
	int rc;

	/* Make sure that nblist is a subset of oblist. */
	for (i = j = 0; j < nblistlen; i++) {
		/*
		 * If we've hit the end of oblist or the next block in oblist
		 * is greater than the next block in nblist, there's at least
		 * one block in nblist which is not in oblist.
		 */
		if ((i == oblistlen) ||
		    ((rc = cmpblock(&oblist[i], &nblist[j])) > 0)) {
			warn0("New machine has data not in old machine!"
			    "  Cannot continue.");
			exit(1);
		}

		/* If we've matched this new block, advance the pointer. */
		if (rc == 0)
			j++;
	}

	/* If the lists are identical, we have nothing to do. */
	if (oblistlen == nblistlen) {
		*cblistlen = 0;
		*cblist = NULL;
		return;
	}

	/* Allocate space for the blocks-to-copy list. */
	*cblistlen = oblistlen - nblistlen;
	if (IMALLOC(*cblist, *cblistlen, struct block)) {
		warnp("Cannot allocate memory");
		exit(1);
	}

	/* Copy names of blocks which need copying. */
	for (i = j = k = 0; i < oblistlen; i++) {
		/* Do we need to copy this block? */
		if ((j == nblistlen) ||
		    (cmpblock(&oblist[i], &nblist[j]) < 0)) {
			(*cblist)[k].class = oblist[i].class;
			memcpy((*cblist)[k].name, oblist[i].name, 32);
			k++;
		} else
			j++;
	}

	/* Sanity check. */
	if (j != nblistlen) {
		warn0("Programmer error: Didn't get to end of new block list");
		exit(1);
	}
	if (k != *cblistlen) {
		warn0("Programmer error: Didn't fill blocks-to-copy list");
		exit(1);
	}
}

static int
callback_read(void * cookie, int status, uint8_t * buf, size_t buflen)
{
	struct reader * R = cookie;

	/* Make sure we succeeded. */
	if (status != 0) {
		warn0("Block read returned failure: %d", status);
		return (-1);
	}

	/* Store the returned buffer. */
	R->buf = buf;
	R->buflen = buflen;

	/* This read is done. */
	R->status = 1;

	/* Success! */
	return (0);
}

static void
copyblocks(struct block * blist, size_t bnum, uint64_t omnum,
    STORAGE_W * SW)
{
	struct reader R[NCONNS];
	size_t i;
	size_t wleft;
	uint8_t * buf;
	size_t buflen;

	/* Initialize NCONNS readers. */
	for (i = 0; i < NCONNS; i++) {
		if ((R[i].SR = storage_read_init(omnum)) == NULL) {
			warnp("Cannot initialize reader");
			exit(1);
		}
		R[i].status = 1;
		R[i].b = NULL;
	}

	/* No blocks have been writ yet. */
	wleft = bnum;

	/* Cycle through readers handling blocks. */
	for (i = 0; ; i = (i + 1) % NCONNS) {
		/* If all the blocks have been writ, stop. */
		if (wleft == 0)
			break;

		/* Spin until this reader is not reading. */
		if (network_spin(&R[i].status)) {
			warnp("Error in network layer");
			exit(1);
		}

		/* If we have a block, write it. */
		if (R[i].b != NULL) {
			/* If this is a metadata file, recrypt it. */
			if (R[i].b->class == 'm') {
				/* Perform the re-cryption. */
				if (multitape_metadata_recrypt(R[i].buf,
				    R[i].buflen, &buf, &buflen)) {
					warnp("Error re-encrypting metadata");
					exit(1);
				}

				/* Throw out the old buffer. */
				free(R[i].buf);
				R[i].buf = buf;
				R[i].buflen = buflen;
			}

			/* Write the block. */
			if (storage_write_file(SW, R[i].buf, R[i].buflen,
			    R[i].b->class, R[i].b->name)) {
				warnp("Error writing block");
				exit(1);
			}
			wleft--;

			/* Report progress. */
			if (wleft % BATCHDOT == 0)
				printf(".");

			/* We don't have a block any more. */
			R[i].b = NULL;
			free(R[i].buf);
		}

		/* If we have blocks to read, read one. */
		if (bnum > 0) {
			/* Assign this block and read it. */
			R[i].status = 0;
			R[i].b = blist;
			if (storage_read_file_callback(R[i].SR, NULL, 0,
			    R[i].b->class, R[i].b->name,
			    callback_read, &R[i])) {
				warnp("Error reading block");
				exit(1);
			}

			/* We've assigned this block. */
			blist++;
			bnum--;
		}
	}

	/* Shut down the readers. */
	for (i = 0; i < NCONNS; i++)
		storage_read_free(R[i].SR);
}

static void
copydirectory(FILE * src, FILE * dst)
{
	uint8_t buf[65536];
	size_t len;

	while ((len = fread(buf, 1, 65536, src)) > 0) {
		if (fwrite(buf, 1, len, dst) != len) {
			warnp("Error writing chunk directory");
			exit(1);
		}
	}
	if (ferror(src)) {
		warnp("Error reading chunk directory");
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	struct stat sb;
	const char *ocachedir, *ncachedir;
	const char *okeyfile, *nkeyfile;
	uint64_t omachinenum, nmachinenum;
	int odirlock, ndirlock;
	struct block *oblist, *nblist, *cblist;
	size_t oblistlen, nblistlen, cblistlen;
	uint8_t olastseq[32], oseqnum[32];
	uint8_t nlastseq[32], nseqnum[32];
	STORAGE_D * SD;
	STORAGE_W * SW;
	size_t bpos, copynum;
	char *odirpath, *ndirpath;
	FILE *odir, *ndir;
	const char * ch;

	WARNP_INIT;

	/* Attempt to avoid buffering stdout since we print progress msgs. */
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Initialize key cache. */
	if (crypto_keys_init()) {
		warnp("Key cache initialization failed");
		exit(1);
	}

	/* No options yet. */
	ocachedir = ncachedir = NULL;
	okeyfile = nkeyfile = NULL;

	/* Parse arguments. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("--oldkey"):
			if (okeyfile != NULL)
				usage();
			okeyfile = optarg;
			break;
		GETOPT_OPTARG("--oldcachedir"):
			if (ocachedir != NULL)
				usage();
			ocachedir = optarg;
			break;
		GETOPT_OPTARG("--newkey"):
			if (nkeyfile != NULL)
				usage();
			nkeyfile = optarg;
			break;
		GETOPT_OPTARG("--newcachedir"):
			if ((ncachedir != NULL) || (argc < 2))
				usage();
			ncachedir = optarg;
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "tarsnap-recrypt %s\n",
			    PACKAGE_VERSION);
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s\n", ch);
			/* FALLTHROUGH */
		GETOPT_DEFAULT:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have processed all the arguments. */
	if (argc != 0)
		usage();
	(void)argv; /* argv is not used beyond this point. */

	/* Make sure we have the necessary options. */
	if ((ocachedir == NULL) || (ncachedir == NULL) ||
	    (okeyfile == NULL) || (nkeyfile == NULL))
		usage();

	/* Make sure the cache directories exist. */
	if (build_dir(ncachedir, "--newcachdir"))
		exit(1);
	if (build_dir(ocachedir, "--oldcachdir"))
		exit(1);

	/* Lock the cache directories. */
	lockdirs(ocachedir, ncachedir, &odirlock, &ndirlock);

	/* Read keys from the new key file. */
	if (keyfile_read(nkeyfile, &nmachinenum,
	    CRYPTO_KEYMASK_SIGN_PRIV | CRYPTO_KEYMASK_ENCR_PUB |
	    CRYPTO_KEYMASK_AUTH_GET | CRYPTO_KEYMASK_AUTH_PUT |
            CRYPTO_KEYMASK_HMAC_FILE_WRITE, 0)) {
		warnp("Cannot read key file: %s", nkeyfile);
		exit(1);
	}

	/* Get a list of blocks in the new machine. */
	printf("Reading list of blocks for new machine...");
	getblist(nmachinenum, &nblist, &nblistlen);
	printf(" done.\n");

	/*
	 * Make sure any pending checkpoint or commit on the new machine is
	 * completed, and get the current sequence # for future reference.
	 */
	printf("Validating new machine state...");
	if (multitape_cleanstate(ncachedir, nmachinenum, 0)) {
		warnp("Cannot complete pending checkpoint or commit");
		exit(1);
	}
	if (multitape_sequence(ncachedir, nlastseq)) {
		warnp("Cannot get sequence number for new machine");
		exit(1);
	}
	printf(" done.\n");

	/*
	 * Read keys from the old key file.  The AUTH_GET key replaces the
	 * key we read from the new key file; this is fine since (now that
	 * we've read the list of blocks) the only thing we'll be doing to
	 * the new machine is writing blocks.
	 */
	if (keyfile_read(okeyfile, &omachinenum,
	    CRYPTO_KEYMASK_SIGN_PUB | CRYPTO_KEYMASK_ENCR_PRIV |
	    CRYPTO_KEYMASK_AUTH_GET | CRYPTO_KEYMASK_AUTH_DELETE |
	    CRYPTO_KEYMASK_HMAC_FILE, 0)) {
		warnp("Cannot read key file: %s", okeyfile);
		exit(1);
	}

	/*
	 * Make sure any pending checkpoint or commit is completed, and start
	 * a storage-layer delete transaction on the old machine.  Doing this
	 * now serves two purposes: First, it ensures that our cached state is
	 * synced with the server (via the sequence # check) and thus that the
	 * directory file we're going to copy across is valid; and second, it
	 * guarantees that if we lose a race against another system accessing
	 * the same machine's data, we won't delete anything (such a scenario
	 * implies PEBKAC, but safety belts are good anyway).
	 */
	printf("Validating old machine state...");
	if (multitape_cleanstate(ocachedir, omachinenum, 1)) {
		warnp("Cannot complete pending checkpoint or commit");
		exit(1);
	}
	if (multitape_sequence(ocachedir, olastseq)) {
		warnp("Cannot get sequence number for old machine");
		exit(1);
	}
	if ((SD =
	    storage_delete_start(omachinenum, olastseq, oseqnum)) == NULL) {
		warnp("Cannot start delete transaction");
		exit(1);
	}
	printf(" done.\n");

	/* Get a list of blocks in the old machine. */
	printf("Reading list of blocks for old machine...");
	getblist(omachinenum, &oblist, &oblistlen);
	printf(" done.\n");

	/* Compare lists of blocks. */
	compareblists(oblist, oblistlen, nblist, nblistlen,
	    &cblist, &cblistlen);

	/* Don't need the list of blocks owned by the new machine any more. */
	free(nblist);

	/* Construct paths to chunk directories. */
	if (asprintf(&ndirpath, "%s/directory", ncachedir) == -1) {
		warnp("asprintf");
		exit(1);
	}
	if (asprintf(&odirpath, "%s/directory", ocachedir) == -1) {
		warnp("asprintf");
		exit(1);
	}

	/*
	 * If the old chunk directory file does not exist, the old machine
	 * must have no blocks; confirm this, and exit (since copying and
	 * deleting zero files is a no-op).
	 */
	if (stat(odirpath, &sb)) {
		/* Errors other than ENOENT are bad. */
		if (errno != ENOENT) {
			warnp("stat(%s)", odirpath);
			exit(1);
		}

		/* Having blocks without a cache directory is bad. */
		if (oblistlen != 0) {
			warn0("Chunk directory is missing: %s", odirpath);
			exit(1);
		}

		/* Nothing to do. */
		exit(0);
	}

	/*
	 * Create an empty directory file for the new machine.  An empty file
	 * is not a valid directory, so if we crash, tarsnap will fail until
	 * the cache directory is reconstructed via --fsck or we are re-run
	 * and allowed to finish the recrypting process.
	 */
	if ((ndir = fopen(ndirpath, "w")) == NULL) {
		warnp("Cannot create chunk directory for new machine");
		exit(1);
	}

	/* Copy blocks to new machine. */
	for (bpos = 0; bpos < cblistlen; bpos += BATCHLEN) {
		/* Report progress. */
		printf("Copying blocks [%zu/%zu]..", bpos / BATCHLEN + 1,
		    (cblistlen + BATCHLEN - 1) / BATCHLEN);

		/* Start a write transaction. */
		if ((SW = storage_write_start(nmachinenum, nlastseq,
		    nseqnum)) == NULL) {
			warnp("Cannot start write transaction");
			exit(1);
		}

		/* Figure out how many blocks we need to copy. */
		if ((copynum = cblistlen - bpos) > BATCHLEN)
			copynum = BATCHLEN;

		/* Copy the blocks. */
		copyblocks(&cblist[bpos], copynum, omachinenum, SW);

		/* End the write transaction. */
		if (storage_write_end(SW)) {
			warnp("Cannot complete write transaction");
			exit(1);
		}

		/* Commit the write transaction. */
		if (multitape_commit(ncachedir, nmachinenum, nseqnum, 0)) {
			warnp("Cannot commit write transaction");
			exit(1);
		}

		/* We have a new last sequence number. */
		memcpy(nlastseq, nseqnum, 32);

		/* We've done this group. */
		printf(". done.\n");
	}

	/* Open the old cache directory... */
	if ((odir = fopen(odirpath, "r")) == NULL) {
		warnp("Cannot read chunk directory for old machine");
		exit(1);
	}

	/* ... and copy its contents into the new cache directory. */
	printf("Updating cache directory...");
	copydirectory(odir, ndir);
	printf(" done.\n");

	/* Close the old and new chunk directories. */
	if (fclose(ndir) || fclose(odir)) {
		warnp("Error closing chunk directory");
		exit(1);
	}

	/* Delete blocks from old machine. */
	for (bpos = 0; bpos < oblistlen; bpos++) {
		/* Report progress. */
		if (bpos % BATCHLEN == 0)
			printf("Deleting blocks [%zu/%zu]..",
			    bpos / BATCHLEN + 1,
			    (oblistlen + BATCHLEN - 1) / BATCHLEN);

		/* Delete a file. */
		if (storage_delete_file(SD, oblist[bpos].class,
		    oblist[bpos].name)) {
			warnp("Error deleting blocks");
			exit(1);
		}

		/* Report any completed progress. */
		if ((bpos == oblistlen - 1) ||
		    (bpos % BATCHLEN == BATCHLEN - 1)) {
			if (storage_delete_flush(SD)) {
				warnp("Error deleting blocks");
				exit(1);
			}
			printf(". done.\n");
		} else if (bpos % BATCHDOT == BATCHDOT - 1)
			printf(".");
	}

	/* We've issued all our deletes. */
	if (storage_delete_end(SD)) {
		warnp("Error deleting blocks");
		exit(1);
	}

	/* Commit the delete transaction and delete the old chunk dir. */
	printf("Committing block deletes...");
	if (multitape_commit(ocachedir, omachinenum, oseqnum, 1)) {
		warnp("Cannot commit delete transaction");
		exit(1);
	}
	if (unlink(odirpath)) {
		warnp("Cannot delete old chunk directory: %s", odirpath);
		exit(1);
	}
	printf(" done.\n");

	/* Free strings allocated by asprintf. */
	free(odirpath);
	free(ndirpath);

	/* Close lock files (not really needed as they autoclose on exit). */
	close(odirlock);
	close(ndirlock);

	return (0);
}
