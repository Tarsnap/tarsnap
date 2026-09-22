/* Actual command handler and hex decoder; archive access is a local fixture. */
#include "platform.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../tar/glue/tape.c"
#include "../../libcperciva/util/hexify.c"

static int mode, opens, closes, items, lists, warnings, scenarios;
static int dummy;

TAPE_S *
statstape_open(uint64_t machine, const char * path)
{
	assert(machine == 17 && path == NULL);
	opens++;
	return (mode == 1 ? NULL : (TAPE_S *)&dummy);
}

int
statstape_close(TAPE_S * tape)
{
	assert(tape == (TAPE_S *)&dummy);
	closes++;
	return (mode == 4 ? -1 : 0);
}

int
statstape_printlist(TAPE_S * tape, int verbose, int nulls, int hashes)
{
	(void)verbose;
	(void)nulls;
	(void)hashes;
	assert(tape == (TAPE_S *)&dummy);
	lists++;
	return (mode == 3 ? -1 : 0);
}

int
statstape_printlist_item(TAPE_S * tape, const uint8_t hash[32],
    int verbose, int nulls, int hashes)
{
	(void)verbose;
	(void)nulls;
	assert(tape == (TAPE_S *)&dummy && hashes == 1);
	assert(hash[0] == 0xaa && hash[31] == 0xaa);
	items++;
	return (mode == 2 ? -1 : 0);
}

void
bsdtar_warnc(struct bsdtar * tar, int code, const char * fmt, ...)
{
	(void)tar;
	(void)code;
	assert(fmt != NULL);
	warnings++;
}

static void
scenario(char ** names, size_t count, int hashes, int fail,
    int expected_error, int expected_opens, int expected_closes,
    int expected_items, int expected_lists)
{
	struct bsdtar tar;

	memset(&tar, 0, sizeof(tar));
	tar.machinenum = 17;
	tar.ntapes = count;
	tar.tapenames = names;
	mode = fail;
	opens = closes = items = lists = warnings = 0;
	tarsnap_mode_list_archives(&tar, hashes);
	assert(tar.return_value == expected_error);
	assert(opens == expected_opens && closes == expected_closes);
	assert(items == expected_items && lists == expected_lists);
	assert((warnings != 0) == expected_error);
	scenarios++;
}

int
main(void)
{
	char good[65], short_hash[64], long_hash[66], bad_hex[65];
	char * names[2];
	char * invalid[4];
	size_t i;

	memset(good, 'a', 64);
	good[64] = '\0';
	memset(short_hash, 'a', 63);
	short_hash[63] = '\0';
	memset(long_hash, 'a', 65);
	long_hash[65] = '\0';
	memcpy(bad_hex, good, sizeof(good));
	bad_hex[31] = 'g';
	invalid[0] = "";
	invalid[1] = "not-a-hash";
	invalid[2] = short_hash;
	invalid[3] = bad_hex;
	for (i = 0; i < 4; i++) {
		names[0] = invalid[i];
		scenario(names, 1, 1, 0, 1, 1, 1, 0, 0);
		names[0] = good;
		names[1] = invalid[i];
		scenario(names, 2, 1, 0, 1, 1, 1, 1, 0);
	}
	/* unhexify consumes 64 digits; trailing input is an existing contract. */
	names[0] = long_hash;
	scenario(names, 1, 1, 0, 0, 1, 1, 1, 0);
	names[0] = names[1] = good;
	scenario(names, 2, 1, 0, 0, 1, 1, 2, 0);
	scenario(names, 1, 0, 0, 1, 0, 0, 0, 0);
	scenario(NULL, 0, 0, 0, 0, 1, 1, 0, 1);
	scenario(names, 1, 1, 1, 1, 1, 0, 0, 0);
	scenario(names, 1, 1, 2, 1, 1, 1, 1, 0);
	scenario(NULL, 0, 1, 3, 1, 1, 1, 0, 1);
	scenario(names, 1, 1, 4, 1, 1, 1, 1, 0);
	printf("PASS: %d archive-list cleanup and control scenarios\n", scenarios);
	return (0);
}
