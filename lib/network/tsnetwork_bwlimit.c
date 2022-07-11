#include <string.h>

#include "events.h"
#include "monoclock.h"

#include "tsnetwork.h"
#include "tsnetwork_internal.h"

#ifdef TSNETWORK_PRINT_SPEED_SECONDS
#include <stdint.h>
#include <stdio.h>

#include "tarsnap_opt.h"
#endif

/* Bandwidth limiting state. */
struct bwlimit {
	double Bps;
	double bucket;
	void * timer_cookie;
	int suspended;
#ifdef TSNETWORK_PRINT_SPEED_SECONDS
	size_t bytes_since_last_print;
#endif
};
static struct bwlimit limit_read;
static struct bwlimit limit_write;

/* Last time tokens were added to buckets. */
static struct timeval tlast;
static int tlast_set = 0;

/* Initialized? */
static int initdone = 0;

/* Update the internal state. */
static int poke(void);

/* Initialize if necessary. */
static void
init(void)
{

	/* Limit to 1 GBps by default. */
	limit_read.Bps = 1000000000.0;

	/* 2s burst. */
	limit_read.bucket = 2 * limit_read.Bps;

	/* No timer yet. */
	limit_read.timer_cookie = NULL;

	/* Traffic not suspended. */
	limit_read.suspended = 0;

#ifdef TSNETWORK_PRINT_SPEED_SECONDS
	limit_read.bytes_since_last_print = 0;
#endif

	/* Write state is the same as the read state. */
	memcpy(&limit_write, &limit_read, sizeof(struct bwlimit));

	/* We've been initialized! */
	initdone = 1;
}

/* Timer wakeup. */
static int
callback_timer(void * cookie)
{
	struct bwlimit * l = cookie;

	/* This timer is no longer running. */
	l->timer_cookie = NULL;

	/* Update state. */
	return (poke());
}

/* Update the state for one direction. */
static int
pokeone(struct bwlimit * l, double t, int op)
{
	double waketime;

	/* Add tokens to the bucket. */
	l->bucket += l->Bps * t;

	/* Overflow the bucket at 2 seconds of bandwidth. */
	if (l->bucket > 2 * l->Bps)
		l->bucket = 2 * l->Bps;

	/* Do we need to re-enable traffic? */
	if ((l->bucket >= 1460) && (l->suspended != 0)) {
		/* Allow traffic to pass. */
		if (network_register_resume(op))
			goto err0;
		l->suspended = 0;
	}

	/* Do we need to block traffic? */
	if ((l->bucket < 1460) && (l->suspended == 0)) {
		/* Stop traffic from running. */
		if (network_register_suspend(op))
			goto err0;
		l->suspended = 1;
	}

	/* If traffic is running, we don't need a timer. */
	if ((l->suspended == 0) && (l->timer_cookie != NULL)) {
		events_timer_cancel(l->timer_cookie);
		l->timer_cookie = NULL;
	}

	/* If traffic is suspended, we need a timer. */
	if ((l->suspended == 1) && (l->timer_cookie == NULL)) {
		/* Wait 10 ms or for 1460 bytes of quota. */
		waketime = (1460 - l->bucket) / l->Bps;
		if (waketime < 0.01)
			waketime = 0.01;

		/* Register a timer. */
		if ((l->timer_cookie =
		    events_timer_register_double(callback_timer,
		    l, waketime)) == NULL)
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

#ifdef TSNETWORK_PRINT_SPEED_SECONDS
static void
print_speed(struct timeval tnow)
{
	static struct timeval torig;
	static struct timeval tlast_printed;
	double tsince_last;

	/* If this is the first time, initialize the static variables. */
	if (!tlast_set) {
		memcpy(&torig, &tnow, sizeof(struct timeval));
		memcpy(&tlast_printed, &tnow, sizeof(struct timeval));
	}

	/* Duration since the last time we printed the speed. */
	tsince_last = timeval_diff(tlast_printed, tnow);

	/* Bail if not enough time has elapsed. */
	if (tsince_last < TSNETWORK_PRINT_SPEED_SECONDS)
		return;

	/* Print speed. */
	fprintf(stderr, "TSNETWORK_PRINT_SPEED_SECONDS\t%.3f\t%.1f\t%.1f\n",
	    timeval_diff(torig, tnow),
	    (double)limit_read.bytes_since_last_print / tsince_last,
	    (double)limit_write.bytes_since_last_print / tsince_last);

	/* Reset stats and record the current time. */
	limit_read.bytes_since_last_print = 0;
	limit_write.bytes_since_last_print = 0;
	memcpy(&tlast_printed, &tnow, sizeof(struct timeval));
}
#endif

/* Update the internal state. */
static int
poke(void)
{
	struct timeval tnow;
	double t;

	/* Get the current time. */
	if (monoclock_get(&tnow))
		goto err0;

	/* Compute the duration since the last poke. */
	if (tlast_set)
		t = timeval_diff(tlast, tnow);
	else
		t = 0.0;

	/* Poke each direction. */
	if (pokeone(&limit_read, t, NETWORK_OP_READ))
		goto err0;
	if (pokeone(&limit_write, t, NETWORK_OP_WRITE))
		goto err0;

#ifdef TSNETWORK_PRINT_SPEED_SECONDS
	if (tarsnap_opt_debug_network_stats)
		print_speed(tnow);
#endif

	/* We have been poked. */
	memcpy(&tlast, &tnow, sizeof(struct timeval));
	tlast_set = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_bwlimit(down, up):
 * Set the bandwidth rate limit to ${down} bytes per second of read bandwidth
 * and ${up} bytes per second of write bandwidth.  The values ${down} and
 * ${up} must be between 8000 and 10^9.
 */
void
network_bwlimit(double down, double up)
{

	/* Initialize if necessary. */
	if (!initdone)
		init();

	/* Record these values for future reference. */
	limit_read.Bps = down;
	limit_write.Bps = up;

	/* Don't allow more than a 2 s burst. */
	if (limit_read.bucket > 2 * limit_read.Bps)
		limit_read.bucket = 2 * limit_read.Bps;
	if (limit_write.bucket > 2 * limit_write.Bps)
		limit_write.bucket = 2 * limit_write.Bps;
}

/**
 * network_bwlimit_get(op, len):
 * Get the amount of instantaneously allowed bandwidth for ${op} operations.
 */
int
network_bwlimit_get(int op, size_t * len)
{

	/* Initialize if necessary. */
	if (!initdone)
		init();

	/* Update state. */
	if (poke())
		goto err0;

	/* Return the appropriate value. */
	if (op == NETWORK_OP_READ)
		*len = (size_t)limit_read.bucket;
	else
		*len = (size_t)limit_write.bucket;

	/*
	 * If the allowed bandwidth is less than one normal-sized TCP segment,
	 * force it to zero, in order to avoid silly windowing.
	 */
	if (*len < 1460)
		*len = 0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_bwlimit_eat(op, len):
 * Consume ${len} bytes of bandwidth quota for ${op} operations.
 */
int
network_bwlimit_eat(int op, size_t len)
{

	/* Initialize if necessary. */
	if (!initdone)
		init();

	/* Eat tokens from the bucket. */
	if (op == NETWORK_OP_READ)
		limit_read.bucket -= (double)len;
	else
		limit_write.bucket -= (double)len;

#ifdef TSNETWORK_PRINT_SPEED_SECONDS
	/* Accumulate sum. */
	if (op == NETWORK_OP_READ)
		limit_read.bytes_since_last_print += len;
	else
		limit_write.bytes_since_last_print += len;
#endif

	/* Update state. */
	return (poke());
}
