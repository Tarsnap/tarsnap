#ifndef _EVENTS_INTERNAL_H_
#define _EVENTS_INTERNAL_H_

#include <sys/time.h>

#include <signal.h>

/* Opaque event structure. */
struct eventrec;

/**
 * events_mkrec(func, cookie):
 * Package ${func}, ${cookie} into a struct eventrec.
 */
struct eventrec * events_mkrec(int (*)(void *), void *);

/**
 * events_freerec(r):
 * Free the eventrec ${r}.
 */
void events_freerec(struct eventrec *);

/**
 * events_immediate_get(void):
 * Remove and return an eventrec structure from the immediate event queue,
 * or return NULL if there are no such events.  The caller is responsible for
 * freeing the returned memory.
 */
struct eventrec * events_immediate_get(void);

/**
 * events_network_select(tv, interrupt_requested):
 * Check for socket readiness events, waiting up to ${tv} time if there are
 * no sockets immediately ready, or indefinitely if ${tv} is NULL.  The value
 * stored in ${tv} may be modified.  If ${*interrupt_requested} is non-zero
 * and a signal is received, exit.
 */
int events_network_select(struct timeval *, volatile sig_atomic_t *);

/**
 * events_network_selectstats_startclock(void):
 * Start the inter-select duration clock: There is a selectable event.
 */
void events_network_selectstats_startclock(void);

/**
 * events_network_selectstats_stopclock(void):
 * Stop the inter-select duration clock: There are no selectable events.
 */
void events_network_selectstats_stopclock(void);

/**
 * events_network_selectstats_select(void):
 * Update inter-select duration statistics in relation to an upcoming
 * select(2) call.
 */
void events_network_selectstats_select(void);

/**
 * events_network_get(void):
 * Find a socket readiness event which was identified by a previous call to
 * events_network_select, and return it as an eventrec structure; or return
 * NULL if there are no such events available.  The caller is responsible for
 * freeing the returned memory.
 */
struct eventrec * events_network_get(void);

/**
 * events_timer_min(timeo):
 * Return via ${timeo} a pointer to the minimum time which must be waited
 * before a timer will expire; or to NULL if there are no timers.  The caller
 * is responsible for freeing the returned pointer.
 */
int events_timer_min(struct timeval **);

/**
 * events_timer_get(r):
 * Return via ${r} a pointer to an eventrec structure corresponding to an
 * expired timer, and delete said timer; or to NULL if there are no expired
 * timers.  The caller is responsible for freeing the returned pointer.
 */
int events_timer_get(struct eventrec **);

#endif /* !_EVENTS_INTERNAL_H_ */
