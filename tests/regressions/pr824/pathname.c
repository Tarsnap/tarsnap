/* Exercise the complete pathname editor; only its strdup failure is synthetic. */
#include "bsdtar_platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *pr824_strdup(const char *);
static void pr824_free(void *);
#ifndef UTIL_SOURCE
#define UTIL_SOURCE "../../../tar/util.c"
#endif
#define strdup pr824_strdup
#define free pr824_free
#include UTIL_SOURCE
#undef strdup
#undef free

void __real_archive_entry_copy_pathname(struct archive_entry *, const char *);
static struct bsdtar state;
static struct archive_entry *item;
static int active, fail_duplicate, duplicates, failed_duplicates, released;
static int copies, null_copies, returned, result = -999;
static void *owned_duplicate;

static char *
pr824_strdup(const char *s)
{
	char *p;

	if (active) {
		duplicates++;
		if (fail_duplicate) {
			failed_duplicates++;
			errno = ENOMEM;
			return NULL;
		}
	}
	p = strdup(s);
	if (active) {
		if (p == NULL)
			exit(70);
		owned_duplicate = p;
	}
	return p;
}

static void
pr824_free(void *p)
{
	if (active && p != NULL && p == owned_duplicate) {
		released++;
		owned_duplicate = NULL;
	}
	free(p);
}

void
__wrap_archive_entry_copy_pathname(struct archive_entry *entry, const char *name)
{
	if (active) {
		copies++;
		if (name == NULL)
			null_copies++;
	}
	/* Keep libarchive's actual NULL and buffer-ownership semantics. */
	__real_archive_entry_copy_pathname(entry, name);
}

int
__wrap_network_select(int block)
{
	(void)block;
	fputs("Unexpected network boundary reached\n", stderr);
	exit(72);
}

static void
hex_string(const char *s)
{
	const unsigned char *p;

	if (s == NULL) {
		fputs("null", stdout);
		return;
	}
	putchar('"');
	for (p = (const unsigned char *)s; *p != '\0'; p++)
		printf("%02x", *p);
	putchar('"');
}

static void
report(void)
{
	active = 0;
	printf("{\"returned\":%d,\"result\":%d,\"duplicates\":%d,"
	    "\"failed_duplicates\":%d,\"released\":%d,\"copies\":%d,"
	    "\"null_copies\":%d,\"warned_lead_slash\":%d,\"path\":",
	    returned, result, duplicates, failed_duplicates, released, copies,
	    null_copies, state.warned_lead_slash);
	hex_string(archive_entry_pathname(item));
	fputs(",\"hardlink\":", stdout);
	hex_string(archive_entry_hardlink(item));
	fputs(",\"symlink\":", stdout);
	hex_string(archive_entry_symlink(item));
	fputs("}\n", stdout);
	cleanup_substitution(&state);
	archive_entry_free(item);
	fflush(stdout);
}

int
main(int argc, char **argv)
{
	if (argc != 9)
		return 64;
	state.progname = "pr824-fixture";
	state.strip_components = atoi(argv[2]);
	state.option_absolute_paths = atoi(argv[3]);
	state.option_quiet = atoi(argv[4]);
	fail_duplicate = atoi(argv[8]);
	item = archive_entry_new();
	if (item == NULL)
		return 70;
	archive_entry_copy_pathname(item, argv[1]);
	if (*argv[5] != '\0')
		add_substitution(&state, argv[5]);
	if (*argv[6] != '\0')
		archive_entry_copy_hardlink(item, argv[6]);
	if (*argv[7] != '\0')
		archive_entry_copy_symlink(item, argv[7]);
	if (atexit(report) != 0)
		return 70;
	active = 1;
	/* Real bsdtar_errc() emits its diagnostic and exits; do not replace it. */
	result = edit_pathname(&state, item);
	returned = 1;
	return 0;
}
