/* Type-contract and real signal-delivery checks; no terminal is modified. */
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "sigquit.h"
#include "ttyfd.h"
#include "warnp.h"

_Static_assert(_Generic(&sigquit_received,
    volatile sig_atomic_t *: 1, default: 0),
    "The signal-handler flag must be volatile sig_atomic_t");

int
ttyfd(void)
{

	/* Exercise the supported non-interactive initialization path. */
	return (-1);
}

void
libcperciva_warn(const char * fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	abort();
}

void
libcperciva_warnx(const char * fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	abort();
}

int
main(void)
{
	struct sigaction original;
	unsigned int i;

	assert(sigaction(SIGQUIT, NULL, &original) == 0);
	for (i = 0; i < 32; i++) {
		assert(sigquit_init() == 0);
		assert(sigquit_received == 0);
		assert(raise(SIGQUIT) == 0);
		assert(sigquit_received == 1);
		assert(raise(SIGQUIT) == 0);
		assert(sigquit_received == 1);
	}
	assert(sigaction(SIGQUIT, &original, NULL) == 0);
	puts("PASS: volatile type contract and 64 real SIGQUIT deliveries");
	return (0);
}
