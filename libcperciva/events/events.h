#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <sys/select.h>

/**
 * events_immediate_register(func, cookie, prio):
 * Register ${func}(${cookie}) to be run the next time events_run is invoked,
 * after immediate events with smaller ${prio} values and before events with
 * larger ${prio} values.  The value ${prio} must be in the range [0, 31].
 * Return a cookie which can be passed to events_immediate_cancel.
 */
void * events_immediate_register(int (*)(void *), void *, int);

/**
 * events_immediate_cancel(cookie):
 * Cancel the immediate event for which the cookie ${cookie} was returned by
 * events_immediate_register.
 */
void events_immediate_cancel(void *);

/* "op" parameter to events_network_register. */
#define EVENTS_NETWORK_OP_READ	0
#define EVENTS_NETWORK_OP_WRITE	1

/**
 * events_network_register(func, cookie, s, op):
 * Register ${func}(${cookie}) to be run when socket ${s} is ready for
 * reading or writing depending on whether ${op} is EVENTS_NETWORK_OP_READ or
 * EVENTS_NETWORK_OP_WRITE.  If there is already an event registration for
 * this ${s}/${op} pair, errno will be set to EEXIST and the function will
 * fail.
 */
int events_network_register(int (*)(void *), void *, int, int);

/**
 * events_network_cancel(s, op):
 * Cancel the event registered for the socket/operation pair ${s}/${op}.  If
 * there is no such registration, errno will be set to ENOENT and the
 * function will fail.
 */
int events_network_cancel(int, int);

/**
 * events_network_selectstats(N, mu, va, max):
 * Return statistics on the inter-select durations since the last time this
 * function was called.
 */
void events_network_selectstats(double *, double *, double *, double *);

/**
 * events_timer_register(func, cookie, timeo):
 * Register ${func}(${cookie}) to be run ${timeo} in the future.  Return a
 * cookie which can be passed to events_timer_cancel or events_timer_reset.
 */
void * events_timer_register(int (*)(void *), void *, const struct timeval *);

/**
 * events_timer_register_double(func, cookie, timeo):
 * As events_timer_register, but ${timeo} is a double-precision floating point
 * value specifying a number of seconds.
 */
void * events_timer_register_double(int (*)(void *), void *, double);

/**
 * events_timer_cancel(cookie):
 * Cancel the timer for which the cookie ${cookie} was returned by
 * events_timer_register.
 */
void events_timer_cancel(void *);

/**
 * events_timer_reset(cookie):
 * Reset the timer for which the cookie ${cookie} was returned by
 * events_timer_register to its initial value.
 */
int events_timer_reset(void *);

/**
 * events_run(void):
 * Run events.  Events registered via events_immediate_register will be run
 * first, in order of increasing ${prio} values; then events associated with
 * ready sockets registered via events_network_register; finally, events
 * associated with expired timers registered via events_timer_register will
 * be run.  If any event function returns a non-zero result, no further
 * events will be run and said non-zero result will be returned; on error,
 * -1 will be returned.  May be interrupted by events_interrupt, in which case
 * 0 will be returned.  If there are runnable events, events_run is guaranteed
 * to run at least one; but it may return while there are still more runnable
 * events.
 */
int events_run(void);

/**
 * events_spin(done):
 * Call events_run until ${done} is non-zero (and return 0), an error occurs (and
 * return -1), or a callback returns a non-zero status (and return the status
 * code from the callback).  May be interrupted by events_interrupt (and return
 * 0).
 */
int events_spin(int *);

/**
 * events_interrupt(void):
 * Halt the event loop after finishing the current event.  This function can
 * be safely called from within a signal handler.
 */
void events_interrupt(void);

/**
 * events_shutdown(void):
 * Deprecated function; does nothing.
 */
void events_shutdown(void);

#endif /* !_EVENTS_H_ */
