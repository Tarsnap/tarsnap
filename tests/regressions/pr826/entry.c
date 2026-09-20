/* Complete native writer, real ccache allocator/free, bounded local file inputs. */
#ifndef ENTRY_SOURCE
#define ENTRY_SOURCE "../../../tar/write.c"
#endif
#include ENTRY_SOURCE
#include "ccache_internal.h"
#include "multitape_internal.h"
#include <stdarg.h>

void * __real_malloc(size_t);
void * __real_calloc(size_t, size_t);
void __real_free(void *);
CCACHE_ENTRY * __real_ccache_entry_lookup(CCACHE *, const char *, const struct stat *, TAPE_W *, int *);
void __real_ccache_entry_free(CCACHE_ENTRY *, TAPE_W *);
int __real_fileutil_open_noatime(const char *, int, int);
int __real_close(int);

struct allocation { void * ptr; int live; };
static struct allocation allocations[512];
static int tracking, used, live, peak, freed, invalid_free;
static int lookup_calls, cleanup_calls, cleanup_nonnull, callback_clears, wrong_cookie;
static int ordinal, fail_at, open_calls, open_files, mode_calls;
static int opened[64], nopened;
static char tape_cookie;
static const char * mode;
static int is(const char * name) { return strcmp(mode, name) == 0; }

void * __wrap_malloc(size_t size) {
    void * ptr;
    if (tracking && ++ordinal == fail_at) { errno = ENOMEM; return NULL; }
    ptr = __real_malloc(size);
    if (tracking && ptr != NULL) {
        if (used == 512) exit(70);
        allocations[used++] = (struct allocation){ptr, 1};
        if (++live > peak) peak = live;
    }
    return ptr;
}
void * __wrap_calloc(size_t count, size_t size) {
    void * ptr;
    if (tracking && ++ordinal == fail_at) { errno = ENOMEM; return NULL; }
    ptr = __real_calloc(count, size);
    if (tracking && ptr != NULL) {
        if (used == 512) exit(70);
        allocations[used++] = (struct allocation){ptr, 1};
        if (++live > peak) peak = live;
    }
    return ptr;
}
void __wrap_free(void * ptr) {
    int i;
    if (tracking && ptr != NULL) {
        for (i = used - 1; i >= 0; i--) {
            if (allocations[i].live && allocations[i].ptr == ptr) {
                allocations[i].live = 0;
                live--; freed++;
                __real_free(ptr);
                return;
            }
        }
        /* No untracked object belongs to a cache lookup in these fixtures. */
        invalid_free++;
        return;
    }
    __real_free(ptr);
}
CCACHE_ENTRY * __wrap_ccache_entry_lookup(CCACHE * cache, const char * path,
    const struct stat * st, TAPE_W * cookie, int * full) {
    CCACHE_ENTRY * result;
    lookup_calls++;
    tracking = 1; ordinal = 0;
    result = __real_ccache_entry_lookup(cache, path, st, cookie, full);
    tracking = 0;
    return result;
}
void __wrap_ccache_entry_free(CCACHE_ENTRY * entry, TAPE_W * cookie) {
    int i, known = entry == NULL;
    cleanup_calls++;
    for (i = 0; i < used; i++)
        if (allocations[i].live && allocations[i].ptr == entry) known = 1;
    if (!known) { invalid_free++; return; }
    cleanup_nonnull += entry != NULL;
    tracking = 1;
    __real_ccache_entry_free(entry, cookie);
    tracking = 0;
}
void __wrap_writetape_setcallback(TAPE_W * cookie,
    int (*chunk)(void *, struct chunkheader *),
    int (*trailer)(void *, const uint8_t *, size_t), void * arg) {
    callback_clears++;
    if (cookie != (TAPE_W *)&tape_cookie || chunk != NULL || trailer != NULL || arg != NULL)
        wrong_cookie++;
}
int __wrap_fileutil_open_noatime(const char * path, int flags, int noatime) {
    int fd;
    open_calls++;
    fd = __real_fileutil_open_noatime(path, flags, noatime);
    if (fd != -1) {
        if (nopened == 64) exit(70);
        opened[nopened++] = fd;
        open_files++;
    }
    return fd;
}
int __wrap_close(int fd) {
    int i;
    for (i = 0; i < nopened; i++) if (opened[i] == fd) {
        opened[i] = -1; open_files--; break;
    }
    return __real_close(fd);
}
int __wrap_archive_write_multitape_setmode(struct archive * a, TAPE_W * cookie, int value) {
    (void)a; (void)value;
    mode_calls++;
    if (cookie != (TAPE_W *)&tape_cookie) wrong_cookie++;
    return 0; /* Native archive output stays in the in-memory writer, not a tape. */
}
int __wrap_network_select(int value) {
    (void)value;
    fprintf(stderr, "Unexpected network/event path\n");
    exit(72);
}

int main(int argc, char ** argv) {
    struct bsdtar tar = {0};
    struct ccache_internal cache = {0};
    struct ccache_record record = {0}, saved;
    struct chunkheader chunk;
    struct stat st = {0};
    struct archive_entry * entry;
    struct archive * out;
    unsigned char output[65536];
    size_t output_size = 0;
    const char * rpath = "existing-cache-key";
    int n, verbose, i, lookup_expected, nonnull_expected, success, tree_ok, passed;
    void ** found;
    if (argc != 5) return 64;
    mode = argv[1]; n = atoi(argv[2]); verbose = atoi(argv[3]);
    if (n < 1 || n > 32 || verbose < 0 || verbose > 1) return 64;
    success = is("success-nocache") || is("success-empty");
    fail_at = is("lookup-first-alloc") ? 1 : is("lookup-second-alloc") ? 2 : 0;
    lookup_expected = is("fresh") || is("existing") || fail_at ? n : 0;
    nonnull_expected = is("fresh") || is("existing") ? n : 0;
    tar.progname = "pr826-fixture";
    tar.verbose = verbose;
    tar.option_noatime = verbose;
    tar.option_dryrun = 2; /* Success controls exercise metadata and descriptor closure only. */
    tar.write_cookie = (TAPE_W *)&tape_cookie;
    tar.chunk_cache = &cache;
    cache.tree = patricia_init();
    if (!cache.tree) return 70;
    st.st_mode = S_IFREG | 0600;
    st.st_size = 16; st.st_ino = 123; st.st_mtime = 1700000000;
    if (is("existing")) {
        /* A tree-owned record must survive abandoned-entry cleanup unchanged. */
        memset(&chunk, 0x5a, sizeof(chunk));
        record.ino = 122; record.size = 15; record.mtime = 1699999990;
        record.nch = 1; record.chp = &chunk; record.age = 9;
        if (patricia_insert(cache.tree, (const uint8_t *)rpath, strlen(rpath), &record)) return 70;
    }
    saved = record;
    if (is("disabled") || is("success-nocache")) tar.chunk_cache = NULL;
    if (is("crunch")) tar.cachecrunch = 2;
    if (is("nonregular")) st.st_mode = S_IFDIR | 0700;
    if (is("success-empty")) st.st_size = 0;
    if ((out = archive_write_new()) == NULL) return 70;
    if (archive_write_set_format_ustar(out) || archive_write_open_memory(out, output, sizeof(output), &output_size)) return 70;
    for (i = 0; i < n; i++) {
        if ((entry = archive_entry_new()) == NULL) return 70;
        archive_entry_copy_stat(entry, &st);
        archive_entry_set_pathname(entry, "entry");
        archive_entry_copy_sourcepath(entry, argv[4]);
        write_entry_backend(&tar, out, entry, is("no-stat") ? NULL : &st,
                            is("no-path") ? NULL : rpath);
        archive_entry_free(entry);
    }
    found = patricia_lookup(cache.tree, (const uint8_t *)rpath, strlen(rpath));
    tree_ok = is("existing") ? found && *found == &record && !memcmp(&record, &saved, sizeof(record))
                             : found == NULL;
    passed = live == 0 && invalid_free == 0 && wrong_cookie == 0 && open_files == 0 &&
             lookup_calls == lookup_expected && cleanup_nonnull == nonnull_expected &&
             callback_clears == nonnull_expected && tree_ok && tar.return_value == 0 &&
             open_calls == (is("success-empty") ? 0 : n) &&
             mode_calls == (is("success-nocache") ? 3*n : is("success-empty") ? 2*n : 0);
    printf("{\"mode\":\"%s\",\"repetitions\":%d,\"verbose\":%d,\"allocated\":%d,"
           "\"freed\":%d,\"live\":%d,\"peak\":%d,\"invalid_free\":%d,"
           "\"lookup_calls\":%d,\"cleanup_calls\":%d,\"cleanup_nonnull\":%d,"
           "\"callback_clears\":%d,\"wrong_cookie\":%d,\"open_calls\":%d,"
           "\"open_files\":%d,\"mode_calls\":%d,\"tree_preserved\":%s,"
           "\"return_value\":%d,\"success_control\":%s,\"passed\":%s}\n",
           mode,n,verbose,used,freed,live,peak,invalid_free,lookup_calls,cleanup_calls,
           cleanup_nonnull,callback_clears,wrong_cookie,open_calls,open_files,mode_calls,
           tree_ok ? "true" : "false",tar.return_value,success ? "true" : "false",passed ? "true" : "false");
    for (i = 0; i < used; i++) if (allocations[i].live) __real_free(allocations[i].ptr);
    patricia_free(cache.tree);
    archive_write_finish(out);
    return passed ? 0 : 1;
}
