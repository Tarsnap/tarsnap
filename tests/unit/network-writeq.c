/* Real queue code, with only its asynchronous transport/clock stubbed. */
#include <sys/time.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "tsnetwork.h"
#include "tsnetwork_cork.h"
#include "tvmath.h"

struct result {
	int calls;
	int status;
	int rc;
};

static network_callback * pending;
static void * pending_cookie;
static int write_fail, clock_fail, dereg_error, dereg_calls;

int
network_cork(int fd)
{
	assert(fd == 41);
	return (0);
}

int
network_uncork(int fd)
{
	assert(fd == 41);
	return (0);
}

int
tvmath_addctime(struct timeval * tv)
{
	(void)tv;
	return (0);
}

int
tvmath_subctime(struct timeval * tv)
{
	(void)tv;
	return (clock_fail ? -1 : 0);
}

int
tsnetwork_write(int fd, const uint8_t * buf, size_t len,
    struct timeval * to0, struct timeval * to1,
    network_callback * callback, void * cookie)
{
	assert(fd == 41 && buf != NULL && len == 1);
	assert(to0 != NULL && to1 != NULL);
	assert(pending == NULL);
	if (write_fail)
		return (-1);
	pending = callback;
	pending_cookie = cookie;
	return (0);
}

static int
complete(int status)
{
	network_callback * callback = pending;
	void * cookie = pending_cookie;

	assert(callback != NULL);
	/* The real network layer deregisters before calling back. */
	pending = NULL;
	pending_cookie = NULL;
	return (callback(cookie, status));
}

int
network_deregister(int fd, int op)
{
	assert(fd == 41 && op == NETWORK_OP_WRITE);
	/* Bound the original no-progress loop without waiting for a hang. */
	assert(++dereg_calls <= 32);
	if (pending != NULL)
		return (complete(NETWORK_STATUS_CANCEL));
	return (dereg_error);
}

static int
finished(void * cookie, int status)
{
	struct result * r = cookie;

	assert(r->calls++ == 0);
	r->status = status;
	return (r->rc);
}

static void
scenario(int count, int mode)
{
	NETWORK_WRITEQ * q;
	struct result results[17] = {{0, 0, 0}};
	struct timeval timeout = {10, 0};
	uint8_t byte = 42;
	int i, expected;

	write_fail = clock_fail = dereg_error = dereg_calls = 0;
	assert(pending == NULL);
	assert((q = network_writeq_init(41)) != NULL);
	assert(network_writeq_cancel(q) == 0);
	assert(dereg_calls == 0);
	for (i = 0; i < count; i++) {
		results[i].rc = i == 1 ? 17 : (i == 2 ? 23 : 0);
		assert(network_writeq_add_internal(q, &byte, 1, &timeout,
		    finished, &results[i], mode == 3) == 0);
	}
	if (mode != 0) {
		write_fail = mode != 3;
		clock_fail = mode == 3;
		/* Completion removes the head; starting its successor fails. */
		assert(complete(NETWORK_STATUS_OK) == -1);
		assert(results[0].calls == 1);
		assert(results[0].status == NETWORK_STATUS_OK);
		assert(pending == NULL);
	}
	if (mode == 2)
		dereg_error = -1;
	expected = mode == 2 ? -1 : 17;
	assert(network_writeq_cancel(q) == expected);
	assert(pending == NULL);
	for (i = mode == 0 ? 0 : 1; i < count; i++) {
		assert(results[i].calls == 1);
		assert(results[i].status == NETWORK_STATUS_CANCEL);
	}
	assert(dereg_calls == (mode == 0 ? count : count - 1));
	assert(network_writeq_cancel(q) == 0);
	/* Empty tail state must still admit and complete another buffer. */
	write_fail = clock_fail = dereg_error = 0;
	assert(network_writeq_add(q, &byte, 1, &timeout,
	    finished, &results[count]) == 0);
	assert(complete(NETWORK_STATUS_OK) == 0);
	assert(results[count].calls == 1);
	assert(results[count].status == NETWORK_STATUS_OK);
	network_writeq_free(q);
}

int
main(void)
{
	int count, mode;

	for (mode = 0; mode < 4; mode++)
		for (count = 2; count <= 16; count++)
			scenario(count, mode);
	network_writeq_free(NULL);
	puts("PASS: 60 queue scenarios, exact callbacks, errors and reuse");
	return (0);
}
