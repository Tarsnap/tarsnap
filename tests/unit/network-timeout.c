/* Real buffer I/O over local socketpairs; the event scheduler is controlled. */
#include <sys/socket.h>
#include <sys/time.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tarsnap_opt.h"
#include "tsnetwork.h"
#include "tsnetwork_internal.h"
#include "tvmath.h"
#include "warnp.h"

int tarsnap_opt_noisy_warnings;
static network_callback * pending;
static void * pending_cookie;
static size_t quota, consumed;
static int calls, observed;

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
	return (0);
}

int
network_bwlimit_get(int op, size_t * len)
{
	assert(op == NETWORK_OP_READ || op == NETWORK_OP_WRITE);
	*len = quota;
	return (0);
}

int
network_bwlimit_eat(int op, size_t len)
{
	assert(op == NETWORK_OP_READ || op == NETWORK_OP_WRITE);
	consumed += len;
	return (0);
}

int
network_register(int fd, int op, struct timeval * timeo,
    network_callback * callback, void * cookie)
{
	assert(fd >= 0 && timeo != NULL);
	assert(op == NETWORK_OP_READ || op == NETWORK_OP_WRITE);
	assert(pending == NULL);
	pending = callback;
	pending_cookie = cookie;
	return (0);
}

void
libcperciva_warn(const char * fmt, ...)
{
	(void)fmt;
	abort();
}

void
libcperciva_warnx(const char * fmt, ...)
{
	(void)fmt;
	abort();
}

static int
finished(void * cookie, int status)
{
	assert(cookie == &observed);
	assert(calls++ == 0);
	observed = status;
	return (37);
}

static int
ready(int status)
{
	network_callback * callback = pending;
	void * cookie = pending_cookie;

	assert(callback != NULL);
	pending = NULL;
	pending_cookie = NULL;
	return (callback(cookie, status));
}

static void
scenario(int writing, size_t partial, int status)
{
	struct timeval idle = {3, 0}, total = {10, 0};
	uint8_t data[4] = {11, 22, 33, 44}, buf[4] = {0};
	int fd[2], expected;

	calls = observed = 0;
	consumed = 0;
	quota = partial == 0 ? sizeof(buf) : partial;
	assert(pending == NULL);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
	if (writing)
		assert(tsnetwork_write(fd[0], data, sizeof(data),
		    &idle, &total, finished, &observed) == 0);
	else
		assert(tsnetwork_read(fd[0], buf, sizeof(buf),
		    &idle, &total, finished, &observed) == 0);
	if (partial != 0) {
		if (!writing)
			assert(write(fd[1], data, partial) == (ssize_t)partial);
		assert(ready(NETWORK_STATUS_OK) == 0);
		assert(calls == 0 && consumed == partial);
		if (writing)
			assert(read(fd[1], buf, partial) == (ssize_t)partial);
		assert(memcmp(buf, data, partial) == 0);
	}
	assert(ready(status) == 37);
	expected = status == NETWORK_STATUS_TIMEOUT && partial == 0 ?
	    NETWORK_STATUS_NODATA : status;
	assert(calls == 1 && observed == expected);
	assert(pending == NULL);
	assert(close(fd[0]) == 0 && close(fd[1]) == 0);
}

static void
complete_transfer(int writing)
{
	struct timeval timeout = {10, 0};
	uint8_t data[4] = {0, 255, 42, 17}, buf[4] = {0};
	int fd[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
	calls = observed = 0;
	consumed = 0;
	quota = 4;
	if (writing)
		assert(tsnetwork_write(fd[0], data, 4, &timeout, &timeout,
		    finished, &observed) == 0);
	else {
		assert(write(fd[1], data, 4) == 4);
		assert(tsnetwork_read(fd[0], buf, 4, &timeout, &timeout,
		    finished, &observed) == 0);
	}
	assert(ready(NETWORK_STATUS_OK) == 37);
	assert(calls == 1 && observed == NETWORK_STATUS_OK);
	assert(consumed == 4 && pending == NULL);
	if (writing)
		assert(read(fd[1], buf, 4) == 4);
	assert(memcmp(buf, data, 4) == 0);
	assert(close(fd[0]) == 0 && close(fd[1]) == 0);
}

int
main(void)
{
	int writing;
	size_t partial;

	for (writing = 0; writing <= 1; writing++) {
		for (partial = 0; partial < 4; partial++) {
			scenario(writing, partial, NETWORK_STATUS_TIMEOUT);
			scenario(writing, partial, NETWORK_STATUS_CANCEL);
			scenario(writing, partial, NETWORK_STATUS_ERR);
			scenario(writing, partial, NETWORK_STATUS_CLOSED);
		}
		complete_transfer(writing);
	}
	puts("PASS: 34 local socketpair timeout, error and success scenarios");
	return (0);
}
