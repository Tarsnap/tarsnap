/* Exercise the real tree with binary keys and every critical-bit position. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "patricia.h"

struct record {
	uint8_t key[514];
	size_t len;
};

struct traversal {
	struct record ** ordered;
	size_t count;
	size_t visited;
	size_t stop;
};

static int
keycmp(const void * a, const void * b)
{
	const struct record * x = *(struct record * const *)a;
	const struct record * y = *(struct record * const *)b;
	size_t len = x->len < y->len ? x->len : y->len;
	int rc;

	if ((rc = memcmp(x->key, y->key, len)) != 0)
		return (rc);
	return ((x->len > y->len) - (x->len < y->len));
}

static int
visit(void * cookie, uint8_t * key, size_t len, void * rec)
{
	struct traversal * t = cookie;
	struct record * expected;

	assert(t->visited < t->count);
	expected = t->ordered[t->visited++];
	assert(rec == expected);
	assert(len == expected->len);
	if (len != 0)
		assert(memcmp(key, expected->key, len) == 0);
	return (t->visited == t->stop ? 17 : 0);
}

int
main(void)
{
	struct record records[261];
	struct record * ordered[261];
	struct traversal t;
	PATRICIA * tree;
	void ** value;
	uint8_t missing[2] = { 0x73, 0x42 };
	size_t i, j, k, n = 261;

	memset(records, 0, sizeof(records));
	assert((tree = patricia_init()) != NULL);
	t.ordered = ordered;
	t.count = t.visited = 0;
	t.stop = (size_t)-1;
	assert(patricia_foreach(tree, visit, &t) == 0);
	assert(t.visited == 0);
	for (i = 0; i < 256; i++) {
		records[i].key[0] = (uint8_t)i;
		records[i].len = 1;
	}
	/* Empty, long shared prefixes, and a NUL-bearing extended key. */
	for (i = 257; i < n; i++) {
		memset(records[i].key, 0xa5, 512);
		records[i].len = i == 257 ? 512 : 513;
		records[i].key[512] = i == 259 ? 0x80 : 0;
	}
	records[260].key[510] = 0;
	for (i = 0; i < n; i++) {
		/* 73 is coprime to 256: no sorted-insertion shortcut. */
		k = i < 256 ? (i * 73) % 256 : i;
		assert(patricia_insert(tree, records[k].key,
		    records[k].len, &records[k]) == 0);
		for (j = 0; j <= i; j++) {
			k = j < 256 ? (j * 73) % 256 : j;
			value = patricia_lookup(tree, records[k].key,
			    records[k].len);
			assert(value != NULL && *value == &records[k]);
		}
	}
	assert(patricia_lookup(tree, missing, sizeof(missing)) == NULL);
	for (i = 0; i < n; i++) {
		assert(patricia_insert(tree, records[i].key,
		    records[i].len, &records[(i + 1) % n]) == 1);
		assert(*patricia_lookup(tree, records[i].key,
		    records[i].len) == &records[i]);
		ordered[i] = &records[i];
	}
	qsort(ordered, n, sizeof(*ordered), keycmp);
	t.count = n;
	t.visited = 0;
	assert(patricia_foreach(tree, visit, &t) == 0);
	assert(t.visited == n);
	t.visited = 0;
	t.stop = 13;
	assert(patricia_foreach(tree, visit, &t) == 17);
	assert(t.visited == 13);
	/* The public lookup API deliberately exposes a mutable record pointer. */
	value = patricia_lookup(tree, records[42].key, records[42].len);
	*value = &records[43];
	assert(*patricia_lookup(tree, records[42].key, 1) == &records[43]);
	*value = NULL;
	assert(patricia_lookup(tree, records[42].key, 1) == NULL);
	patricia_free(tree);
	patricia_free(NULL);
	puts("PASS: 261 binary keys, 34191 prefix lookups, duplicates, traversal");
	return (0);
}
