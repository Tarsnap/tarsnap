/* Actual complete tar/write.c with a test-only entrypoint; native objects link it. */
#ifndef APPEND_SOURCE
#define APPEND_SOURCE "../../../tar/write.c"
#endif
#include APPEND_SOURCE
#include <stdarg.h>

struct archive * __real_archive_read_new(void);
int __real_archive_read_finish(struct archive *);
const char * __real_archive_error_string(struct archive *);
void __real_bsdtar_warnc(struct bsdtar *, int, const char *, ...);

struct lifetime { struct archive * object; int live; };
static struct lifetime objects[512];
static int created, finished, live, peak, late_reads, duplicate_finishes;
static int warnings, failures, successes, bad_returns, tape_success;
static const char * mode;
static unsigned char empty_tar[1024];
static int cookie;

struct archive * __wrap_archive_read_new(void) {
    struct archive * a = __real_archive_read_new();
    if (!a || created == 512) exit(70);
    objects[created++] = (struct lifetime){a, 1};
    live++;
    if (live > peak) peak = live;
    return a;
}
static int active_index(struct archive * a) {
    int i;
    for (i = created - 1; i >= 0; i--)
        if (objects[i].live && objects[i].object == a) return i;
    return -1;
}
int __wrap_archive_read_finish(struct archive * a) {
    int i = active_index(a);
    if (i < 0) { duplicate_finishes++; return ARCHIVE_FATAL; }
    objects[i].live = 0;
    live--;
    finished++;
    return __real_archive_read_finish(a);
}
const char * __wrap_archive_error_string(struct archive * a) {
    if (active_index(a) < 0) {
        late_reads++;
        return "TEST: error string read after input archive was released";
    }
    return __real_archive_error_string(a);
}
void __wrap_bsdtar_warnc(struct bsdtar * tar, int code, const char * fmt, ...) {
    char text[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    warnings++;
    __real_bsdtar_warnc(tar, code, "%s", text);
}
void * __wrap_archive_read_open_multitape(struct archive * a, uint64_t machine,
    const char * name) {
    (void)machine;
    if (tape_success) {
        if (archive_read_open_memory(a, empty_tar, sizeof(empty_tar))) exit(71);
        return &cookie;
    }
    archive_set_error(a, ENOENT, "Synthetic unavailable archive: %s", name);
    return NULL;
}
int __wrap_network_select(int block) {
    (void)block;
    /* No scenario contains entries or permits a network/event-processing path. */
    fprintf(stderr, "Unexpected network_select reached\n");
    exit(72);
}
static void call_file(struct bsdtar * tar, struct archive * out,
    const char * name, int error) {
    int rc = append_archive_filename(tar, out, name);
    if (rc != 0) bad_returns++;
    if (error) failures++; else successes++;
    if (tar->return_value != (failures != 0)) bad_returns++;
}
static void call_tape(struct bsdtar * tar, struct archive * out, int error) {
    int rc;
    tape_success = !error;
    rc = append_archive_tarsnap(tar, out, "synthetic-test-archive");
    if (rc != 0) bad_returns++;
    if (error) failures++; else successes++;
    if (tar->return_value != (failures != 0)) bad_returns++;
}
int main(int argc, char ** argv) {
    struct bsdtar tar = {0};
    struct archive * out;
    int n, i, passed;
    if (argc != 7) return 64;
    mode = argv[1]; n = atoi(argv[2]);
    if (n < 1 || n > 64) return 64;
    tar.progname = "pr828-fixture";
    siginfo_init(&tar);
    out = archive_write_new();
    if (!out) return 70;
    for (i = 0; i < n; i++) {
        if (!strcmp(mode, "missing")) call_file(&tar, out, argv[3], 1);
        else if (!strcmp(mode, "junk")) call_file(&tar, out, argv[4], 1);
        else if (!strcmp(mode, "directory")) call_file(&tar, out, argv[5], 1);
        else if (!strcmp(mode, "file-ok")) call_file(&tar, out, argv[6], 0);
        else if (!strcmp(mode, "tape-fail")) call_tape(&tar, out, 1);
        else if (!strcmp(mode, "tape-ok")) call_tape(&tar, out, 0);
        else if (!strcmp(mode, "file-recover")) {
            call_file(&tar, out, argv[3], 1); call_file(&tar, out, argv[6], 0);
        } else if (!strcmp(mode, "tape-recover")) {
            call_tape(&tar, out, 1); call_tape(&tar, out, 0);
        } else if (!strcmp(mode, "mixed")) {
            call_file(&tar, out, argv[3], 1); call_file(&tar, out, argv[4], 1);
            call_tape(&tar, out, 1);
            call_file(&tar, out, argv[6], 0); call_tape(&tar, out, 0);
        } else return 64;
    }
    passed = created == failures + successes && finished == created && live == 0 &&
        peak == 1 && late_reads == 0 && duplicate_finishes == 0 &&
        warnings == failures && bad_returns == 0;
    printf("{\"mode\":\"%s\",\"repetitions\":%d,\"created\":%d,\"finished\":%d,"
           "\"live\":%d,\"peak\":%d,\"late_error_reads\":%d,\"duplicate_finishes\":%d,"
           "\"warnings\":%d,\"failures\":%d,\"successes\":%d,\"bad_returns\":%d,"
           "\"return_value\":%d,\"passed\":%s}\n",
           mode, n, created, finished, live, peak, late_reads, duplicate_finishes,
           warnings, failures, successes, bad_returns, tar.return_value, passed ? "true" : "false");
    /* Record baseline leaks before bounded teardown of actual allocated readers. */
    for (i = 0; i < created; i++) if (objects[i].live) __real_archive_read_finish(objects[i].object);
    archive_write_finish(out);
    siginfo_done(&tar);
    return passed ? 0 : 1;
}
