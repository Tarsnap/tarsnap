#ifndef TEST_ALLOCATIONS_H_
#define TEST_ALLOCATIONS_H_

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Faults apply only to source compiled with the explicit macros below. */
static void * unit_allocations[256];
static size_t unit_live, unit_requests, unit_fail_at;

static inline void *
unit_malloc(size_t len)
{
	void * p;
	size_t i;

	unit_requests++;
	if (unit_fail_at != 0 && unit_requests == unit_fail_at) {
		errno = ENOMEM;
		return (NULL);
	}
	/* A conforming allocator may return NULL for a zero-size request. */
	if (len == 0)
		return (NULL);
	if ((p = malloc(len)) == NULL)
		return (NULL);
	for (i = 0; i < 256; i++) {
		if (unit_allocations[i] == NULL) {
			unit_allocations[i] = p;
			unit_live++;
			return (p);
		}
	}
	abort();
}

static inline void
unit_free(void * p)
{
	size_t i;

	if (p != NULL) {
		for (i = 0; i < 256; i++) {
			if (unit_allocations[i] == p) {
				unit_allocations[i] = NULL;
				assert(unit_live > 0);
				unit_live--;
				break;
			}
		}
	}
	free(p);
}

static inline char *
unit_strdup(const char * s)
{
	size_t len = strlen(s) + 1;
	char * p = unit_malloc(len);

	if (p != NULL)
		memcpy(p, s, len);
	return (p);
}

static inline void *
unit_memset(void * p, int value, size_t len)
{
	/* Even a zero-length memset requires a valid destination. */
	assert(p != NULL);
	return (memset(p, value, len));
}

static inline void
unit_reset(size_t fail_at)
{
	assert(unit_live == 0);
	unit_requests = 0;
	unit_fail_at = fail_at;
}

#endif /* !TEST_ALLOCATIONS_H_ */
