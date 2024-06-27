#include "platform.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "ctassert.h"
#include "dirutil.h"
#include "rwhashtab.h"
#include "sysendian.h"
#include "warnp.h"

#include "chunks.h"
#include "chunks_internal.h"

/* On-disk extra data statistics structure; integers are little-endian. */
struct chunkstats_external {
	uint8_t nchunks[8];	/* Number of files. */
	uint8_t s_len[8];	/* Sum of file lengths. */
	uint8_t s_zlen[8];	/* Sum of compressed lengths. */
};
CTASSERT(sizeof(struct chunkstats_external) == 24);

/* On-disk chunk metadata structure; integers are little-endian. */
struct chunkdata_external {
	uint8_t hash[32];	/* HMAC of chunk. */
	uint8_t len[4];		/* Length of chunk. */
	uint8_t zlen[4];	/* Compressed length of chunk. */
	uint8_t nrefs[4];	/* Number of existing tapes using this. */
	uint8_t ncopies[4];	/* Number of copies of this chunk. */
};
CTASSERT(sizeof(struct chunkdata_external) == 48);

static int callback_write(void * rec, void * cookie);
static int callback_free(void * rec, void * cookie);

/**
 * callback_write(rec, cookie):
 * Convert chunkdata record ${rec} into a struct chunkdata_external and
 * write it to the FILE * ${cookie}; but don't write entries with nrefs == 0.
 */
static int
callback_write(void * rec, void * cookie)
{
	struct chunkdata_external che;
	struct chunkdata * ch = rec;
	FILE * f = cookie;

	/* If nrefs == 0, return without writing anything. */
	if (ch->nrefs == 0)
		return (0);

	/* Convert to on-disk format. */
	memcpy(che.hash, ch->hash, 32);
	le32enc(che.len, ch->len);
	le32enc(che.zlen, ch->zlen_flags & CHDATA_ZLEN);
	le32enc(che.nrefs, ch->nrefs);
	le32enc(che.ncopies, ch->ncopies);

	/* Write. */
	if (fwrite(&che, sizeof(che), 1, f) != 1) {
		warnp("Error writing to chunk directory");
		return (-1);
	}

	/* Success! */
	return (0);
}

/**
 * callback_free(rec, cookie):
 * If the chunkdata record ${rec} was allocated via malloc(3), free it.
 */
static int
callback_free(void * rec, void * cookie)
{
	struct chunkdata * ch = rec;

	(void)cookie;	/* UNUSED */

	if (ch->zlen_flags & CHDATA_MALLOC)
		free(rec);

	/* Success! */
	return (0);
}

/**
 * chunks_directory_read(cachepath, dir, stats_unique, stats_all, stats_extra,
 *     mustexist, statstape):
 * Read stats_extra statistics (statistics on non-chunks which are stored)
 * and the chunk directory (if present) from "${cachepath}/directory" into
 * memory allocated and assigned to ${*dir}; and return a hash table
 * populated with struct chunkdata records.  Populate stats_all with
 * statistics for all the chunks listed in the directory (counting
 * multiplicity) and populate stats_unique with statistics reflecting the
 * unique chunks.  If ${mustexist}, error out if the directory does not exist.
 * If ${statstape}, allocate struct chunkdata_statstape records instead.
 */
RWHASHTAB *
chunks_directory_read(const char * cachepath, void ** dir,
    struct chunkstats * stats_unique, struct chunkstats * stats_all,
    struct chunkstats * stats_extra, int mustexist, int statstape)
{
	struct chunkdata_external che;
	struct chunkstats_external cse;
	struct stat sb;
	RWHASHTAB * HT;
	char * s;
	struct chunkdata * p = NULL;
	struct chunkdata_statstape * ps = NULL;
	FILE * f;
	size_t numchunks;

	/* Zero statistics. */
	chunks_stats_zero(stats_unique);
	chunks_stats_zero(stats_all);
	chunks_stats_zero(stats_extra);

	/* Create a hash table to hold the chunkdata structures. */
	HT = rwhashtab_init(offsetof(struct chunkdata, hash), 32);
	if (HT == NULL)
		goto err0;

	/* Bail if we're not using a cache directory. */
	if (cachepath == NULL) {
		*dir = NULL;
		return (HT);
	}

	/* Construct the string "${cachepath}/directory". */
	if (asprintf(&s, "%s/directory", cachepath) == -1) {
		warnp("asprintf");
		goto err1;
	}
	if (stat(s, &sb)) {
		/* Could not stat ${cachepath}/directory.  Error? */
		if (errno != ENOENT) {
			warnp("stat(%s)", s);
			goto err2;
		}

		/* The directory doesn't exist; complain if mustexist != 0. */
		if (mustexist) {
			warn0("Error reading cache directory from %s",
			    cachepath);
			goto err2;
		}

		/*
		 * ${cachepath}/directory does not exist; set ${*dir} to NULL
		 * and return the empty hash table.
		 */
		free(s);
		*dir = NULL;
		return (HT);
	}

	/*
	 * Make sure the directory file isn't too large or too small, in
	 * order to avoid any possibility of integer overflows.
	 */
	if ((sb.st_size < 0) ||
	    ((sizeof(off_t) > sizeof(size_t)) && (sb.st_size > SIZE_MAX))) {
		warn0("on-disk directory has insane size (%jd bytes): %s",
		    (intmax_t)(sb.st_size), s);
		goto err2;
	}

	/* Make sure the directory file isn't too small (different message). */
	if ((size_t)sb.st_size < sizeof(struct chunkstats_external)) {
		warn0("on-disk directory is too small (%jd bytes): %s",
		    (intmax_t)(sb.st_size), s);
		goto err2;
	}

	/* Make sure the number of chunks is an integer. */
	if (((size_t)sb.st_size - sizeof(struct chunkstats_external)) %
	    (sizeof(struct chunkdata_external))) {
		warn0("on-disk directory is corrupt: %s", s);
		goto err2;
	}

	/* Compute the number of on-disk chunks. */
	numchunks =
	    ((size_t)sb.st_size - sizeof(struct chunkstats_external)) /
	    sizeof(struct chunkdata_external);

	/* Make sure we don't get an integer overflow. */
	if (numchunks >= SIZE_MAX / sizeof(struct chunkdata_statstape)) {
		warn0("on-disk directory is too large: %s", s);
		goto err2;
	}

	/*
	 * Allocate memory to ${*dir} large enough to store a struct
	 * chunkdata or struct chunkdata_statstape for each struct
	 * chunkdata_external in ${cachepath}/directory.
	 */
	if (statstape) {
		ps = malloc(numchunks * sizeof(struct chunkdata_statstape));
		*dir = ps;
	} else {
		p = malloc(numchunks * sizeof(struct chunkdata));
		*dir = p;
	}
	if (*dir == NULL)
		goto err2;

	/* Open the directory file. */
	if ((f = fopen(s, "r")) == NULL) {
		warnp("fopen(%s)", s);
		goto err3;
	}

	/* Read the extra files statistics. */
	if (fread(&cse, sizeof(cse), 1, f) != 1) {
		warnp("fread(%s)", s);
		goto err4;
	}
	stats_extra->nchunks = le64dec(cse.nchunks);
	stats_extra->s_len = le64dec(cse.s_len);
	stats_extra->s_zlen = le64dec(cse.s_zlen);

	/* Read the chunk structures. */
	for (; numchunks != 0; numchunks--) {
		/* Set p to point at the struct chunkdata. */
		if (statstape)
			p = &ps->d;

		/* Read the file one record at a time... */
		if (fread(&che, sizeof(che), 1, f) != 1) {
			warnp("fread(%s)", s);
			goto err4;
		}

		/* ... creating struct chunkdata records... */
		memcpy(p->hash, che.hash, 32);
		p->len = le32dec(che.len);
		p->zlen_flags = le32dec(che.zlen);
		p->nrefs = le32dec(che.nrefs);
		p->ncopies = le32dec(che.ncopies);

		/* ... inserting them into the hash table... */
		if (rwhashtab_insert(HT, p))
			goto err4;

#if UINT32_MAX > SSIZE_MAX
		/* ... paranoid check for number of copies... */
		if (p->ncopies > SSIZE_MAX)
			warn0("More than %zd copies of a chunk; "
			    "data is ok but stats may be inaccurate",
			    SSIZE_MAX);
#endif

		/* ... and updating the statistics. */
		chunks_stats_add(stats_unique, p->len, p->zlen_flags, 1);
		chunks_stats_add(stats_all, p->len, p->zlen_flags,
		    (ssize_t)p->ncopies);

		/* Sanity check. */
		if ((p->len == 0) || (p->zlen_flags == 0) || (p->nrefs == 0)) {
			warn0("on-disk directory is corrupt: %s", s);
			goto err4;
		}

		/* Move to next record. */
		if (statstape)
			ps++;
		else
			p++;
	}
	if (fclose(f)) {
		warnp("fclose(%s)", s);
		goto err3;
	}

	/* Free string allocated by asprintf. */
	free(s);

	/* Success! */
	return (HT);

err4:
	if (fclose(f))
		warnp("fclose");
err3:
	free(*dir);
err2:
	free(s);
err1:
	rwhashtab_free(HT);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * chunks_directory_write(cachepath, HT, stats_extra, suff):
 * Write stats_extra statistics and the contents of the hash table ${HT} of
 * struct chunkdata records to a new chunk directory in
 * "${cachepath}/directory${suff}".
 */
int
chunks_directory_write(const char * cachepath, RWHASHTAB * HT,
    struct chunkstats * stats_extra, const char * suff)
{
	struct chunkstats_external cse;
	FILE * f;
	char * s;
	int fd;

	/* The caller must pass the cachepath, and a suffix to use. */
	assert(cachepath != NULL);
	assert(suff != NULL);

	/* Construct the path to the new chunk directory. */
	if (asprintf(&s, "%s/directory%s", cachepath, suff) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Create the new chunk directory. */
	if ((f = fopen(s, "w")) == NULL) {
		warnp("fopen(%s)", s);
		goto err1;
	}

	/* Write the extra files statistics. */
	le64enc(cse.nchunks, stats_extra->nchunks);
	le64enc(cse.s_len, stats_extra->s_len);
	le64enc(cse.s_zlen, stats_extra->s_zlen);
	if (fwrite(&cse, sizeof(cse), 1, f) != 1) {
		warnp("Error writing to chunk directory");
		goto err2;
	}

	/* Write the hash table entries to the new chunk directory. */
	if (rwhashtab_foreach(HT, callback_write, f))
		goto err2;

	/* Call fsync on the new chunk directory and close it. */
	if (fflush(f)) {
		warnp("fflush(%s)", s);
		goto err2;
	}
	if ((fd = fileno(f)) == -1) {
		warnp("fileno(%s)", s);
		goto err2;
	}
	if (fsync(fd)) {
		warnp("fsync(%s)", s);
		goto err2;
	}
	if (fclose(f)) {
		warnp("fclose(%s)", s);
		goto err1;
	}

	/* Free string allocated by asprintf. */
	free(s);

	/* Success! */
	return (0);

err2:
	if (fclose(f))
		warnp("fclose");
err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * chunks_directory_exists(cachepath):
 * Return 1 if the /directory file exists within ${cachepath}, 0 if it does
 * not, or -1 if there is an error.
 */
int
chunks_directory_exists(const char * cachepath)
{
	char * directory_filename;
	struct stat sb;
	int rc;

	/* Prepare filename. */
	if (asprintf(&directory_filename, "%s/directory", cachepath) == -1) {
		rc = -1;
		goto done;
	}

	/* Check if file exists. */
	if (stat(directory_filename, &sb) == 0) {
		/* File exists. */
		rc = 1;
	} else {
		if (errno == ENOENT) {
			/* File does not exist. */
			rc = 0;
		} else {
			/* Other error. */
			warnp("stat(%s)", directory_filename);
			rc = -1;
		}
	}

	/* Clean up memory. */
	free(directory_filename);

done:
	/* Return result code. */
	return (rc);
}

/**
 * chunks_directory_free(HT, dir):
 * Free the hash table ${HT} of struct chunkdata records, all of its
 * elements, and ${dir}.
 */
void
chunks_directory_free(RWHASHTAB * HT, void * dir)
{

	/* Free records in the hash table. */
	rwhashtab_foreach(HT, callback_free, NULL);

	/* Free the hash table itself. */
	rwhashtab_free(HT);

	/* Free the records which were allocated en masse. */
	free(dir);
}

/**
 * chunks_directory_commit(cachepath, osuff, nsuff):
 * If ${cachepath}/directory${osuff} exists, move it to
 * ${cachepath}/directory${nsuff} (replacing anything already there).
 */
int
chunks_directory_commit(const char * cachepath, const char * osuff,
    const char * nsuff)
{
	struct stat sbs;
	struct stat sbt;
	char * s;
	char * t;

	/* The caller must pass the cachepath, and suffices to use. */
	assert(cachepath != NULL);
	assert(nsuff != NULL);
	assert(osuff != NULL);

	/* Construct file names. */
	if (asprintf(&s, "%s/directory%s", cachepath, nsuff) == -1) {
		warnp("asprintf");
		goto err0;
	}
	if (asprintf(&t, "%s/directory%s", cachepath, osuff) == -1) {
		warnp("asprintf");
		goto err1;
	}

	/*
	 * If ${cachedir}/directory.tmp does not exist, the transaction was
	 * already committed from the perspective of the chunk layer; so we
	 * can free memory and return.
	 */
	if (lstat(t, &sbt)) {
		if (errno == ENOENT)
			goto done;

		warnp("lstat(%s)", t);
		goto err2;
	}

	/*
	 * If ${cachedir}/directory exists and is not the same file as
	 * ${cachedir}/directory.tmp, remove ${cachedir}/directory and
	 * create a hard link from ${cachedir}/directory.tmp.
	 */
	if (lstat(s, &sbs)) {
		if (errno != ENOENT) {
			warnp("lstat(%s)", s);
			goto err2;
		}
	} else {
		if (sbs.st_ino != sbt.st_ino) {
			/* Remove ${cachedir}/directory. */
			if (unlink(s)) {
				warnp("unlink(%s)", s);
				goto err2;
			}
		} else {
			/*
			 * We're replaying and we've already linked the two
			 * paths; skip ahead to unlinking the .tmp file, as
			 * otherwise link(2) will fail with EEXIST.
			 */
			goto linkdone;
		}
	}

	/*-
	 * We want to move ${t} to ${s} in a crash-proof way.  Unfortunately
	 * the POSIX rename(2) syscall merely guarantees that if ${s} already
	 * exists then ${s} will always exist -- not that the file being
	 * renamed will always exist.  Depending on how crash-proof the
	 * filesystem is, that second requirement might not be satisfied.
	 *
	 * Ideally we would like to solve this problem by creating a hard
	 * link, syncing the directory, then unlinking the old file; but we
	 * might be running on a filesystem/OS which doesn't support hard
	 * links (e.g., FAT32).
	 *
	 * If the link(2) call fails with ENOSYS (sensible failure code for
	 * not supporting hard links) or EPERM (Linux's idea of a joke?), we
	 * fall back to using rename(2) instead of link/sync/unlink.
	 */

	/* Create a link from ${cachedir}/directory.tmp. */
	if (link(t, s)) {
		if ((errno != ENOSYS) && (errno != EPERM)) {
			warnp("link(%s, %s)", t, s);
			goto err2;
		}

		/* Use rename(2) instead. */
		if (rename(t, s)) {
			warnp("rename(%s, %s)", t, s);
			goto err2;
		}
	} else {
linkdone:
		/* Make sure ${cachedir} is flushed to disk. */
		if (dirutil_fsyncdir(cachepath))
			goto err2;

		/* Remove ${cachedir}/directory.tmp. */
		if (unlink(t)) {
			warnp("unlink(%s)", t);
			goto err2;
		}
	}

	/* Finally, sync the directory one last time. */
	if (dirutil_fsyncdir(cachepath))
		goto err2;

done:
	free(t);
	free(s);

	/* Success! */
	return (0);

err2:
	free(t);
err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}
