#include <sys/time.h>

#include "monoclock.h"

#include "events.h"
#include "events_internal.h"

/* Time when inter-select duration clock started. */
static struct timeval st;
static int running = 0;

/* Statistics on inter-select durations. */
static double N = 0.0;
static double mu = 0.0;
static double M2 = 0.0;
static double max = 0.0;

/**
 * events_network_selectstats_startclock(void):
 * Start the inter-select duration clock: There is a selectable event.
 */
void
events_network_selectstats_startclock(void)
{

	/* If the clock is already running, return silently. */
	if (running)
		return;

	/* Get the current time; return silently on error. */
	if (monoclock_get(&st))
		return;

	/* The clock is now running. */
	running = 1;
}

/**
 * events_network_selectstats_stopclock(void):
 * Stop the inter-select duration clock: There are no selectable events.
 */
void
events_network_selectstats_stopclock(void)
{

	/* The clock is no longer running. */
	running = 0;
}

/**
 * events_network_selectstats_select(void):
 * Update inter-select duration statistics in relation to an upcoming
 * select(2) call.
 */
void
events_network_selectstats_select(void)
{
	struct timeval tnow;
	double t, d;

	/* If the clock is not running, return silently. */
	if (!running)
		return;

	/* If we can't get the current time, fail silently. */
	if (monoclock_get(&tnow))
		goto done;

	/* Compute inter-select duration in seconds. */
	t = timeval_diff(st, tnow);

	/* Adjust statistics.  We track running mean, variance * N, and max. */
	N += 1.0;
	d = t - mu;
	mu += d / N;
	M2 += d * (t - mu);
	if (max < t)
		max = t;

done:
	/* The clock is no longer running. */
	running = 0;
}

/**
 * events_network_selectstats(N, mu, va, max):
 * Return statistics on the inter-select durations since the last time this
 * function was called.
 */
void
events_network_selectstats(double * _N, double * _mu, double * _va,
    double * _max)
{

	/* Copy statistics out. */
	*_N = N;
	*_mu = mu;
	if (N > 1.0)
		*_va = M2 / (N - 1.0);
	else
		*_va = 0.0;
	*_max = max;

	/* Zero statistics. */
	N = mu = M2 = max = 0.0;
}
