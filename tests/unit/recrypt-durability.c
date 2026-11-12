/* Run the complete CLI over local cache files and an already-copied fixture. */
#include "platform.h"

#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE * observed_fopen(const char *, const char *);
static int observed_fclose(FILE *);
#define fopen observed_fopen
#define fclose observed_fclose
#include "../../recrypt/recrypt.c"
#undef fclose
#undef fopen
#include "../../libcperciva/util/asprintf.c"

static int failure, block_count, file_synced, directory_synced, tracefd;
static FILE * oldfile, * newfile;
static int dummy;

static void
trace(const char * event)
{
	size_t len = strlen(event);

	assert(write(tracefd, event, len) == (ssize_t)len);
	assert(write(tracefd, "\n", 1) == 1);
}

static FILE *
observed_fopen(const char * path, const char * mode)
{
	FILE * f = fopen(path, mode);

	if (strcmp(mode, "w") == 0)
		newfile = f;
	else if (strcmp(mode, "r") == 0)
		oldfile = f;
	return (f);
}

static int
observed_fclose(FILE * f)
{
	int is_new = f == newfile;
	int is_old = f == oldfile;
	int rc = fclose(f);

	if (is_new) {
		newfile = NULL;
		trace("CLOSE_NEW");
	}
	if (is_old) {
		oldfile = NULL;
		trace("CLOSE_OLD");
	}
	if ((failure == 3 && is_new) || (failure == 4 && is_old)) {
		errno = EIO;
		return (EOF);
	}
	return (rc);
}

int
fileutil_fsync(FILE * f, const char * path)
{
	assert(f == newfile && path != NULL);
	trace(failure == 1 ? "FILE_SYNC_FAIL" : "FILE_SYNC");
	if (failure == 1) {
		errno = EIO;
		return (-1);
	}
	assert(fflush(f) == 0 && fsync(fileno(f)) == 0);
	file_synced = 1;
	return (0);
}

int
dirutil_fsyncdir(const char * path)
{
	int fd;

	assert(file_synced && newfile == NULL && oldfile == NULL);
	trace(failure == 2 ? "DIR_SYNC_FAIL" : "DIR_SYNC");
	if (failure == 2) {
		errno = EIO;
		return (-1);
	}
	assert((fd = open(path, O_RDONLY)) >= 0);
	assert(fsync(fd) == 0 && close(fd) == 0);
	directory_synced = 1;
	return (0);
}

int
crypto_keys_init(void)
{
	assert(getenv("TEST_FAILURE") != NULL);
	assert(getenv("TEST_BLOCKS") != NULL && getenv("TEST_TRACE") != NULL);
	failure = atoi(getenv("TEST_FAILURE"));
	block_count = atoi(getenv("TEST_BLOCKS"));
	assert(failure >= 0 && failure <= 4);
	assert(block_count >= 0 && block_count <= 3);
	assert((tracefd = open(getenv("TEST_TRACE"),
	    O_WRONLY | O_CREAT | O_TRUNC, 0600)) >= 0);
	return (0);
}

const char *
crypto_keys_missing(int mask)
{
	assert(mask != 0);
	return (NULL);
}

int
keyfile_read(const char * name, uint64_t * machine, int keys, int force,
    enum passphrase_entry method, const char * arg)
{
	(void)method;
	assert(keys != 0 && force == 0 && arg == NULL);
	assert(strcmp(name, "fixture-new") == 0 ||
	    strcmp(name, "fixture-old") == 0);
	*machine = strcmp(name, "fixture-new") == 0 ? 2 : 1;
	return (0);
}

int
build_dir(const char * path, const char * option)
{
	struct stat st;

	assert(option != NULL);
	assert(stat(path, &st) == 0 && S_ISDIR(st.st_mode));
	return (0);
}

int
multitape_lock(const char * path)
{
	char filename[1024];

	assert(snprintf(filename, sizeof(filename), "%s/lock", path) > 0);
	return (open(filename, O_CREAT | O_RDWR, 0600));
}

int
multitape_cleanstate(const char * path, uint64_t machine,
    uint8_t key, int * modified)
{
	assert(path != NULL && modified != NULL);
	assert((machine == 1 && key == 1) || (machine == 2 && key == 0));
	return (0);
}

int
multitape_sequence(const char * path, uint8_t seqnum[32])
{
	assert(path != NULL);
	memset(seqnum, 42, 32);
	return (0);
}

STORAGE_D *
storage_delete_start(uint64_t machine, const uint8_t last[32],
    uint8_t next[32])
{
	assert(machine == 1 && last[0] == 42);
	memset(next, 43, 32);
	return ((STORAGE_D *)&dummy);
}

int
storage_directory_read(uint64_t machine, char class, int key,
    uint8_t ** files, size_t * nfiles)
{
	size_t i;

	assert((machine == 1 || machine == 2) && key == 0);
	assert(class == 'c' || class == 'm' || class == 'i');
	*nfiles = class == 'c' ? (size_t)block_count : 0;
	*files = *nfiles == 0 ? NULL : malloc(*nfiles * 32);
	assert(*nfiles == 0 || *files != NULL);
	for (i = 0; i < *nfiles; i++)
		memset(*files + i * 32, (int)i + 1, 32);
	return (0);
}

int
storage_delete_file(STORAGE_D * s, char class, const uint8_t hash[32])
{
	assert(s == (STORAGE_D *)&dummy && class == 'c');
	assert(hash[0] >= 1 && hash[0] <= block_count);
	if (!file_synced || !directory_synced) {
		trace("DELETE_BEFORE_SYNC");
		return (-1);
	}
	trace("DELETE");
	return (0);
}

int
storage_delete_flush(STORAGE_D * s)
{
	assert(s == (STORAGE_D *)&dummy);
	return (0);
}

int
storage_delete_end(STORAGE_D * s)
{
	assert(s == (STORAGE_D *)&dummy);
	return (0);
}

int
multitape_commit(const char * path, uint64_t machine,
    const uint8_t sequence[32], uint8_t key, int * modified)
{
	assert(path != NULL && machine == 1 && key == 1);
	assert(sequence[0] == 43 && modified != NULL);
	if (!file_synced || !directory_synced) {
		trace("COMMIT_BEFORE_SYNC");
		return (-1);
	}
	trace("COMMIT");
	*modified = 1;
	return (0);
}

/* The resumed fixture has every block on both machines: copying is forbidden. */
STORAGE_W *
storage_write_start(uint64_t machine, const uint8_t last[32], uint8_t next[32])
{
	(void)machine;
	(void)last;
	(void)next;
	abort();
}

int
storage_write_file(STORAGE_W * s, uint8_t * buf, size_t len,
    char class, const uint8_t name[32])
{
	(void)s;
	(void)buf;
	(void)len;
	(void)class;
	(void)name;
	abort();
}

int
storage_write_end(STORAGE_W * s)
{
	(void)s;
	abort();
}

STORAGE_R *
storage_read_init(uint64_t machine)
{
	(void)machine;
	abort();
}

void
storage_read_free(STORAGE_R * s)
{
	(void)s;
	abort();
}

int
storage_read_file_callback(STORAGE_R * s, uint8_t * buf, size_t len,
    char class, const uint8_t name[32],
    int (* callback)(void *, int, uint8_t *, size_t), void * cookie)
{
	(void)s;
	(void)buf;
	(void)len;
	(void)class;
	(void)name;
	(void)callback;
	(void)cookie;
	abort();
}

int
multitape_metadata_recrypt(uint8_t * old, size_t len,
    uint8_t ** new, size_t * newlen)
{
	(void)old;
	(void)len;
	(void)new;
	(void)newlen;
	abort();
}

int
network_spin(int * done)
{
	(void)done;
	abort();
}

void
warnp_setprogname(const char * name)
{
	assert(name != NULL);
}

void
libcperciva_warn(const char * fmt, ...)
{
	assert(fmt != NULL);
}

void
libcperciva_warnx(const char * fmt, ...)
{
	assert(fmt != NULL);
}
