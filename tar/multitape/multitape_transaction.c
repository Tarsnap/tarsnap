#include "bsdtar_platform.h"

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chunks.h"
#include "dirutil.h"
#include "hexify.h"
#include "hexlink.h"
#include "storage.h"
#include "warnp.h"

#include "multitape_internal.h"

/**
 * multitape_cleanstate(cachedir, machinenum, key):
 * Complete any pending commit.  The value ${key} should be 0 if the write
 * access key should be used to sign a commit request, or 1 if the delete
 * access key should be used.
 */
int
multitape_cleanstate(const char * cachedir, uint64_t machinenum, uint8_t key)
{
	char * s, * t;
	uint8_t seqnum[32];

	/* Make sure ${cachedir} is flushed to disk. */
	if (dirutil_fsyncdir(cachedir))
		goto err0;

	/* Read ${cachedir}/commit_m if it exists. */
	if (asprintf(&s, "%s/commit_m", cachedir) == -1) {
		warnp("asprintf");
		goto err0;
	}
	if (hexlink_read(s, seqnum, 32)) {
		if (errno != ENOENT)
			goto err1;

		/*
		 * ${cachedir}/commit_m doesn't exist; we don't need to do
		 * anything.
		 */
		goto done;
	}

	/* Ask the chunk layer to commit the transaction. */
	if (chunks_transaction_commit(cachedir))
		goto err1;

	/* Ask the storage layer to commit the transaction. */
	if (storage_transaction_commit(machinenum, seqnum, key))
		goto err1;

	/* Remove ${cachedir}/cseq if it exists. */
	if (asprintf(&t, "%s/cseq", cachedir) == -1) {
		warnp("asprintf");
		goto err1;
	}
	if (unlink(t)) {
		/* ENOENT isn't a problem. */
		if (errno != ENOENT) {
			warnp("unlink(%s)", t);
			goto err2;
		}
	}

	/* Store the new sequence number. */
	if (hexlink_write(t, seqnum, 32))
		goto err2;
	free(t);

	/* Make sure ${cachedir} is flushed to disk. */
	if (dirutil_fsyncdir(cachedir))
		goto err1;

	/* Delete ${cachedir}/commit_m. */
	if (unlink(s)) {
		warnp("unlink(%s)", s);
		goto err1;
	}

	/* Make sure ${cachedir} is flushed to disk. */
	if (dirutil_fsyncdir(cachedir))
		goto err1;

done:
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

/**
 * multitape_commit(cachedir, machinenum, seqnum):
 * Commit the most recent transaction.  The value ${key} is defined as in
 * multitape_cleanstate.
 */
int
multitape_commit(const char * cachedir, uint64_t machinenum,
    const uint8_t seqnum[32], uint8_t key)
{
	char * s;

	/* Make ${cachedir}/commit_m point to ${seqnum}. */
	if (asprintf(&s, "%s/commit_m", cachedir) == -1) {
		warnp("asprintf");
		goto err0;
	}
	if (hexlink_write(s, seqnum, 32))
		goto err1;
	free(s);

	/* Complete the transaction. */
	if (multitape_cleanstate(cachedir, machinenum, key))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_lock(cachedir):
 * Lock the given cache directory using lockf(3); return the file descriptor
 * of the lock file, or -1 on error.
 */
int
multitape_lock(const char * cachedir)
{
	char * s;
	int fd;

	/* Open ${cachedir}/lockf. */
	if (asprintf(&s, "%s/lockf", cachedir) == -1) {
		warnp("asprintf");
		goto err0;
	}
	if ((fd = open(s, O_CREAT | O_RDWR, 0666)) == -1) {
		warnp("open(%s)", s);
		goto err1;
	}

	/* Lock the file. */
	while (lockf(fd, F_TLOCK, 0)) {
		/* Retry on EINTR. */
		if (errno == EINTR)
			continue;

		/* Already locked? */
		if (errno == EACCES || errno == EAGAIN) {
			warn0("Transaction already in progress");
			goto err2;
		}

		/* Something went wrong. */
		warnp("lockf(%s)", s);
		goto err2;
	}

	/* Free string allocated by asprintf. */
	free(s);

	/* Success! */
	return (fd);

err2:
	close(fd);
err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * multitape_sequence(cachedir, seqnum):
 * Return the sequence number of the last committed transaction in the
 * cache directory ${cachedir}, or 0 if no transactions have ever been
 * committed.
 */
int
multitape_sequence(const char * cachedir, uint8_t seqnum[32])
{
	char * s;

	/* Read the link ${cachedir}/cseq. */
	if (asprintf(&s, "%s/cseq", cachedir) == -1) {
		warnp("asprintf");
		goto err0;
	}
	if (hexlink_read(s, seqnum, 32)) {
		if (errno != ENOENT)
			goto err1;

		/* ENOENT means the sequence number is zero. */
		memset(seqnum, 0, 32);
	}
	free(s);

	/* Success! */
	return (0);

err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}
