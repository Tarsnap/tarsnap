/* PR830 regression: real writer + Patricia + stdio, bounded local fault seams. */
#include "platform.h"
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "asprintf.h"
#include "ccache_internal.h"
#include "dirutil.h"
#include "fileutil.h"
#include "multitape_internal.h"
#include "patricia.h"
#include "sysendian.h"
#include "warnp.h"
#include "ccache.h"

static void * checked_malloc(size_t);
static void checked_free(void *);
static int checked_asprintf(char **, const char *, ...);
static FILE * checked_fopen(const char *, const char *);
static int checked_fclose(FILE *);
static size_t checked_fwrite(const void *, size_t, size_t, FILE *);
static int checked_foreach(PATRICIA *, int (*)(void *, uint8_t *, size_t, void *), void *);
static int checked_fsync(FILE *, const char *);
static int checked_dirsync(const char *);
static int checked_unlink(const char *);
static int checked_rename(const char *, const char *);
static void quiet_warning(const char *, ...);

#undef asprintf
#undef warnp
#undef warn0
#define malloc checked_malloc
#define free checked_free
#define asprintf checked_asprintf
#define fopen checked_fopen
#define fclose checked_fclose
#define fwrite checked_fwrite
#define patricia_foreach checked_foreach
#define fileutil_fsync checked_fsync
#define dirutil_fsyncdir checked_dirsync
#define unlink checked_unlink
#define rename checked_rename
#define warnp quiet_warning
#define warn0 quiet_warning
#ifndef CCACHE_SOURCE
#define CCACHE_SOURCE "../../../tar/ccache/ccache_write.c"
#endif
#include CCACHE_SOURCE
#undef malloc
#undef free
#undef asprintf
#undef fopen
#undef fclose
#undef fwrite
#undef patricia_foreach
#undef fileutil_fsync
#undef dirutil_fsyncdir
#undef unlink
#undef rename
#undef warnp
#undef warn0

struct allocation { void * ptr; int live; int buffer; };
static struct allocation allocations[64];
static size_t used;
static int invalid_frees, files_open, printf_calls, malloc_calls;
static int write_calls, close_calls, walk_calls, buffer_frees;
static const char * scenario;
static int parameter;
static char poison; /* asprintf failure may leave an unspecified output value. */

static int is(const char * name) { return strcmp(scenario, name) == 0; }
static void quiet_warning(const char * fmt, ...) { (void)fmt; }
static void * track(size_t size, int buffer) {
    void * p = malloc(size ? size : 1);
    if (p == NULL || used == sizeof(allocations) / sizeof(allocations[0]))
        exit(70);
    allocations[used++] = (struct allocation){p, 1, buffer};
    return p;
}
static void * checked_malloc(size_t size) {
    malloc_calls++;
    if (is("malloc") && malloc_calls == parameter) {
        errno = ENOMEM;
        return NULL;
    }
    return track(size, 1);
}
static void checked_free(void * p) {
    size_t i;
    if (p == NULL) return;
    for (i = used; i > 0; i--) {
        if (allocations[i - 1].live && allocations[i - 1].ptr == p) {
            allocations[i - 1].live = 0;
            buffer_frees += allocations[i - 1].buffer;
            free(p);
            return;
        }
    }
    /* Record an invalid/double free without invoking undefined libc behavior. */
    invalid_frees++;
}
static int checked_asprintf(char ** out, const char * fmt, ...) {
    va_list ap, copy;
    int n;
    printf_calls++;
    if ((is("asprintf") && printf_calls == parameter) || is("remove-asprintf")) {
        *out = &poison;
        errno = ENOMEM;
        return -1;
    }
    va_start(ap, fmt);
    va_copy(copy, ap);
    n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) exit(70);
    *out = track((size_t)n + 1, 0);
    if (vsnprintf(*out, (size_t)n + 1, fmt, ap) != n) exit(70);
    va_end(ap);
    return n;
}
static FILE * checked_fopen(const char * path, const char * mode) {
    FILE * f;
    if (is("fopen")) { errno = EACCES; return NULL; }
    f = fopen(path, mode);
    if (f != NULL) files_open++;
    return f;
}
static int checked_fclose(FILE * f) {
    int rc = fclose(f);
    files_open--;
    close_calls++;
    if (is("fclose") && close_calls == 1) { errno = EIO; return EOF; }
    return rc;
}
static size_t checked_fwrite(const void * p, size_t size, size_t count, FILE * f) {
    write_calls++;
    if (is("fwrite") && write_calls == parameter) { errno = ENOSPC; return 0; }
    return fwrite(p, size, count, f);
}
struct walk_context {
    int (*callback)(void *, uint8_t *, size_t, void *);
    void * cookie;
    int pass, seen;
};
static int walk_one(void * v, uint8_t * key, size_t len, void * rec) {
    struct walk_context * w = v;
    int rc = w->callback(w->cookie, key, len, rec);
    w->seen++;
    if (rc) return rc;
    if ((is("record-after") && w->pass == 2 && w->seen == parameter) ||
        (is("data-after") && w->pass == 3 && w->seen == parameter)) {
        errno = EIO;
        return -1;
    }
    return 0;
}
static int checked_foreach(PATRICIA * tree,
    int (*callback)(void *, uint8_t *, size_t, void *), void * cookie) {
    struct walk_context w = {callback, cookie, ++walk_calls, 0};
    if ((is("count") && w.pass == 1) ||
        (is("record-before") && w.pass == 2) ||
        (is("data-before") && w.pass == 3)) { errno = EIO; return -1; }
    return patricia_foreach(tree, walk_one, &w);
}
static int checked_fsync(FILE * f, const char * path) {
    (void)path;
    if (is("fsync")) { errno = EIO; return -1; }
    return fflush(f) || fsync(fileno(f)) ? -1 : 0;
}
static int checked_dirsync(const char * path) {
    int fd, rc;
    if (is("dirsync")) { errno = EIO; return -1; }
    if ((fd = open(path, O_RDONLY | O_DIRECTORY)) == -1) return -1;
    rc = fsync(fd);
    if (close(fd)) rc = -1;
    return rc;
}
static int checked_unlink(const char * path) {
    if (is("unlink") || is("remove-unlink")) { errno = EACCES; return -1; }
    return unlink(path);
}
static int checked_rename(const char * from, const char * to) {
    if (is("rename")) { errno = EIO; return -1; }
    return rename(from, to);
}

#define RECORDS 6
static struct ccache_record records[RECORDS];
static struct chunkheader chunks[RECORDS];
static uint8_t trailers[RECORDS][3];
static char keys[RECORDS][256];
static int active[RECORDS];
static const size_t lengths[RECORDS] = {12, 80, 20, 140, 30, 200};
static void make_tree(struct ccache_internal * cache, int n) {
    int i, j;
    if ((cache->tree = patricia_init()) == NULL) exit(70);
    for (i = 0; i < n; i++) {
        memset(keys[i], 'x', lengths[i]);
        snprintf(keys[i], 8, "root/%d-", i);
        keys[i][7] = 'x';
        keys[i][lengths[i]] = 0;
        records[i].ino = (ino_t)(100 + i);
        records[i].size = 1000 + i;
        records[i].mtime = 1700000000 + i;
        records[i].nch = 1;
        records[i].tlen = records[i].tzlen = 3;
        records[i].age = i;
        memset(&chunks[i], i + 1, sizeof(chunks[i]));
        for (j = 0; j < 3; j++) trailers[i][j] = (uint8_t)(i + j + 50);
        records[i].chp = &chunks[i];
        records[i].ztrailer = trailers[i];
        active[i] = 1;
        if (is("skipped") || (is("mixed") && i % 2 == 0)) {
            active[i] = 0;
            if (i % 3 == 0) records[i].nch = records[i].tlen = 0;
            else if (i % 3 == 1) records[i].age = MAXAGE + 1;
            else records[i].mtime = -1;
        }
        if (patricia_insert(cache->tree, (uint8_t *)keys[i], strlen(keys[i]), &records[i])) exit(70);
    }
}
static int verify_file(const char * path, int n) {
    FILE * f = fopen(path, "rb");
    uint8_t count[4], payload[sizeof(struct chunkheader)];
    struct ccache_record_external ext;
    char previous[256] = "", current[256];
    size_t plen, slen;
    int i, expected = 0, ok = 1;
    if (!f) return 0;
    for (i = 0; i < n; i++) expected += active[i];
    if (fread(count, 4, 1, f) != 1 || le32dec(count) != (uint32_t)expected) ok = 0;
    for (i = 0; i < n && ok; i++) {
        if (!active[i]) continue;
        if (fread(&ext, sizeof(ext), 1, f) != 1) { ok = 0; break; }
        plen = le32dec(ext.prefixlen); slen = le32dec(ext.suffixlen);
        if (plen > strlen(previous) || plen + slen >= sizeof(current)) { ok = 0; break; }
        memcpy(current, previous, plen);
        if (fread(current + plen, 1, slen, f) != slen) { ok = 0; break; }
        current[plen + slen] = 0;
        if (strcmp(current, keys[i]) || le64dec(ext.ino) != (uint64_t)records[i].ino ||
            le64dec(ext.size) != (uint64_t)records[i].size ||
            le64dec(ext.mtime) != (uint64_t)records[i].mtime || le64dec(ext.nch) != 1 ||
            le32dec(ext.tlen) != 3 || le32dec(ext.tzlen) != 3 ||
            le32dec(ext.age) != (uint32_t)records[i].age + 1) ok = 0;
        strcpy(previous, current);
    }
    for (i = 0; i < n && ok; i++) {
        if (!active[i]) continue;
        if (fread(payload, sizeof(chunks[i]), 1, f) != 1 || memcmp(payload, &chunks[i], sizeof(chunks[i]))) ok = 0;
        if (fread(payload, 3, 1, f) != 1 || memcmp(payload, trailers[i], 3)) ok = 0;
    }
    if (ok && fgetc(f) != EOF) ok = 0;
    fclose(f);
    return ok;
}
static int is_old_file(const char * path) {
    FILE * f = fopen(path, "rb");
    char b[16] = {0};
    int ok;
    if (!f) return 0;
    ok = fread(b, 1, sizeof(b), f) == 8 && memcmp(b, "oldcache", 8) == 0;
    fclose(f);
    return ok;
}
int main(int argc, char ** argv) {
    struct ccache_internal cache = {0};
    char directory[] = "/tmp/tarsnap-pr830-XXXXXX", path[256], temp[256];
    FILE * old;
    int n, rc, expected, content_ok = 1, old_ok, live = 0, buffers = 0, passed, remove_mode;
    size_t i;
    if (argc != 4) return 64;
    scenario = argv[1]; n = atoi(argv[2]); parameter = atoi(argv[3]);
    if (n < 0 || n > RECORDS || mkdtemp(directory) == NULL) return 70;
    snprintf(path, sizeof(path), "%s/cache", directory);
    snprintf(temp, sizeof(temp), "%s/cache.new", directory);
    if ((old = fopen(path, "wb")) == NULL) return 70;
    if (fwrite("oldcache", 8, 1, old) != 1 || fclose(old)) return 70;
    remove_mode = strncmp(scenario, "remove-", 7) == 0;
    expected = (is("success") || is("empty") || is("mixed") || is("skipped") ||
                is("remove-existing") || is("remove-missing")) ? 0 : -1;
    if (is("remove-missing") && unlink(path)) return 70;
    if (remove_mode) {
        rc = ccache_remove(directory);
        content_ok = rc == 0 ? access(path, F_OK) == -1 : is_old_file(path);
    } else {
        make_tree(&cache, n);
        rc = ccache_write(&cache, directory);
        if (expected == 0) content_ok = verify_file(path, n);
        else if (!is("rename") && !is("dirsync")) content_ok = is_old_file(path);
        /* Existing unlink-before-rename behavior is outside PR830. */
        patricia_free(cache.tree);
    }
    old_ok = is_old_file(path);
    for (i = 0; i < used; i++) if (allocations[i].live) { live++; buffers += allocations[i].buffer; }
    passed = rc == expected && !live && !invalid_frees && !files_open && content_ok;
    printf("{\"scenario\":\"%s\",\"records\":%d,\"parameter\":%d,\"result\":%d,"
           "\"expected_result\":%d,\"live_allocations\":%d,\"live_buffers\":%d,"
           "\"invalid_frees\":%d,\"open_files\":%d,\"write_calls\":%d,"
           "\"walk_calls\":%d,\"buffer_allocations\":%d,\"buffer_frees\":%d,"
           "\"content_ok\":%s,\"old_cache_present\":%s,\"passed\":%s}\n",
           scenario, n, parameter, rc, expected, live, buffers, invalid_frees, files_open,
           write_calls, walk_calls, malloc_calls, buffer_frees, content_ok ? "true" : "false",
           old_ok ? "true" : "false", passed ? "true" : "false");
    /* Release measured baseline leaks after recording, keeping repeated runs bounded. */
    for (i = 0; i < used; i++) if (allocations[i].live) free(allocations[i].ptr);
    unlink(path); unlink(temp); rmdir(directory);
    return passed ? 0 : 1;
}
