#include "bsdtar_platform.h"
#include "bsdtar.h"
#include "archive.h"
#include "archive_entry.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_target;

/*
 * Interpose strdup for this test executable.  Normal calls retain strdup
 * semantics; only the final normalized pathname "alpha" is forced to fail.
 */
char *
strdup(const char *s)
{
	char *p;
	size_t len;

	if (fail_target && strcmp(s, "alpha") == 0) {
		errno = ENOMEM;
		return (NULL);
	}

	len = strlen(s) + 1;
	if ((p = malloc(len)) == NULL)
		return (NULL);
	memcpy(p, s, len);
	return (p);
}

int
main(int argc, char *argv[])
{
	struct archive_entry *entry;
	struct bsdtar bsdtar;
	int force_failure;

	if (argc != 2)
		return (64);
	if (strcmp(argv[1], "control") == 0)
		force_failure = 0;
	else if (strcmp(argv[1], "fail") == 0)
		force_failure = 1;
	else
		return (64);

	memset(&bsdtar, 0, sizeof(bsdtar));
	bsdtar.progname = "edit-pathname-oom";
	bsdtar.option_quiet = 1;

	if ((entry = archive_entry_new()) == NULL)
		return (70);
	archive_entry_copy_pathname(entry, "/alpha");

	fail_target = force_failure;
	if (edit_pathname(&bsdtar, entry) != 0)
		return (71);
	fail_target = 0;

	/*
	 * The fixed failure case must have exited through bsdtar_errc() above.
	 * Returning here proves the allocation failure escaped that guard.
	 */
	if (force_failure) {
		archive_entry_free(entry);
		return (72);
	}

	if (archive_entry_pathname(entry) == NULL ||
	    strcmp(archive_entry_pathname(entry), "alpha") != 0) {
		archive_entry_free(entry);
		return (73);
	}

	archive_entry_free(entry);
	return (0);
}
