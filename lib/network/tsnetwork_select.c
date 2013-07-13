#include "bsdtar_platform.h"

#include "events.h"

#include "tsnetwork_internal.h"

#include "tsnetwork.h"

/* Callback from network_select with zero timeout. */
static int
callback_dontsleep(void * cookie)
{
	int * done = cookie;

	/* We've been called. */
	*done = 1;

	/* Success! */
	return (0);
}

/**
 * network_select(blocking):
 * Call select(2) on file descriptors provided via network_register and make
 * callbacks as appropriate (including timeouts).  Callbacks are deregistered
 * before being performed.  If ${blocking} is non-zero, allow select(2) to
 * block waiting for descriptors and timeouts.  Stop performing callbacks and
 * return the first non-zero value returned by a callback if one occurs.
 */
int
network_select(int blocking)
{
	int done = 0;

	/*
	 * If we want to avoid blocking on descriptors and timeouts, register
	 * a callback for "right now", and spin until that callback occurs.
	 * Otherwise, just let events run.
	 */
	if (blocking == 0) {
		if (events_timer_register_double(callback_dontsleep,
		    &done, 0.0) == NULL)
			goto err0;
		return (events_spin(&done));
	} else {
		return (events_run());
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_spin(done):
 * Repeatedly call network_select until either an error occurs or the value
 * pointed to by ${done} is non-zero.
 */
int
network_spin(int * done)
{

	/* Hand this off to the events system. */
	return (events_spin(done));
}

/**
 * network_getselectstats(N, mu, va, max):
 * Return and zero statistics on the time between select(2) calls.
 */
void
network_getselectstats(double * NN, double * mu, double * va, double * max)
{

	/* Hand off to events code. */
	events_network_selectstats(NN, mu, va, max);
}

/**
 * network_fini(void):
 * Free resources associated with the network subsystem.
 */
void
network_fini(void)
{

	/* Clean up network registrations. */
	network_register_fini();

	/* Clean up sleeps. */
	network_sleep_fini();

	/* Clean the underlying events system. */
	events_shutdown();
}
