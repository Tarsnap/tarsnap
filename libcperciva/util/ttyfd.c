#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "ttyfd.h"

/**
 * ttyfd(void):
 * Attempt to return a file descriptor to the attached terminal.  In order of
 * priority, try to open the terminal device, as returned by ctermid(3); then
 * use standard error, standard input, or standard output if any of them are
 * terminals.
 */
int
ttyfd(void)
{
	char ttypath[L_ctermid];
	int fd;

	/* Ask for a path to the TTY device. */
	ctermid(ttypath);

	/* If we got a path, try to open it. */
	if (ttypath[0] != '\0') {
		if ((fd = open(ttypath, O_WRONLY | O_NOCTTY)) != -1)
			return (fd);
	}

	/*
	 * Use standard error/input/output if one of them is a terminal.  We
	 * don't check for dup failing because if it fails once it's going to
	 * fail every time.
	 */
	if (isatty(STDERR_FILENO))
		return (dup(STDERR_FILENO));
	if (isatty(STDIN_FILENO))
		return (dup(STDIN_FILENO));
	if (isatty(STDOUT_FILENO))
		return (dup(STDOUT_FILENO));

	/* Sorry, couldn't produce a terminal descriptor. */
	return (-1);
}
