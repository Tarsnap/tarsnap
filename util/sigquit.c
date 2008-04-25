#include "bsdtar_platform.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "warnp.h"

#include "sigquit.h"

/* Saved terminal settings. */
static struct termios tc_saved;

static void sigquit_handler(int);
static void termios_restore(void);

/**
 * sigquit_handler(sig):
 * Record that SIGQUIT has been received.
 */
static void
sigquit_handler(int sig)
{

	(void)sig;	/* UNUSED */

	sigquit_received = 1;
}

/**
 * termios_restore():
 * Restore the saved tc_saved termios state.
 */
static void
termios_restore(void)
{

	/*
	 * Discard return value; we're exiting anyway and there's nothing
	 * that we can do to remedy the situation if the system cannot
	 * restore the previous terminal settings.
	 */
	(void)tcsetattr(STDIN_FILENO, TCSANOW, &tc_saved);
}

/**
 * sigquit_init():
 * Prepare to catch SIGQUIT and ^Q, and zero sigquit_received.
 */
int
sigquit_init(void)
{
	struct termios tc_new;
	size_t i;

	/* We haven't seen SIGQUIT yet... */
	sigquit_received = 0;

	/* ... but when it happens, we want to catch it. */
	if (signal(SIGQUIT, sigquit_handler) == SIG_ERR) {
		warnp("signal(SIGQUIT)");
		goto err0;
	}

	/* Get current terminal settings for stdin. */
	if (tcgetattr(STDIN_FILENO, &tc_saved)) {
		/*
		 * If stdin isn't a TTY, or doesn't exist (i.e., the other
		 * end of the pipe was closed) we're not going to remap ^Q
		 * to SIGQUIT, and we don't need to unmap it on exit.
		 */
		if ((errno == ENOTTY) || (errno == ENXIO))
			goto done;

		warnp("tcgetattr(stdin)");
		goto err0;
	}

	/* Restore the terminal settings on exit(3). */
	if (atexit(termios_restore)) {
		warn("atexit");
		goto err0;
	}

	/* Copy terminal settings. */
	memcpy(&tc_new, &tc_saved, sizeof(struct termios));

	/* Remove any meaning which ^Q already has. */
	for (i = 0; i < NCCS; i++) {
		if (tc_new.c_cc[i] == ('q' & 0x1f))
			tc_new.c_cc[i] = _POSIX_VDISABLE;
	}

	/* Set VQUIT to ^Q. */
	tc_new.c_cc[VQUIT] = 'q' & 0x1f;

	/* Set new terminal settings. */
	if (tcsetattr(STDIN_FILENO, TCSANOW, &tc_new)) {
		warnp("tcsetattr(stdin)");
		goto err0;
	}

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}
