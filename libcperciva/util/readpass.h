#ifndef READPASS_H_
#define READPASS_H_

/* Avoid namespace collisions with other "readpass" functions. */
#ifdef readpass
#undef readpass
#endif
#define readpass libcperciva_readpass

/**
 * readpass(passwd, prompt, confirmprompt, devtty):
 * If ${devtty} is 0, read a password from stdin.  If ${devtty} is 1, read a
 * password from /dev/tty if possible; if not, read from stdin.  If ${devtty}
 * is 2, read a password from /dev/tty if possible; if not, exit with an error.
 * If reading from a tty (either /dev/tty or stdin), disable echo and prompt
 * the user by printing ${prompt} to stderr.  If ${confirmprompt} is non-NULL,
 * read a second password (prompting if a terminal is being used) and repeat
 * until the user enters the same password twice.  Return the password as a
 * malloced NUL-terminated string via ${passwd}.
 */
int readpass(char **, const char *, const char *, int);

/**
 * readpass_file(passwd, filename):
 * Read a passphrase from ${filename} and return it as a malloced
 * NUL-terminated string via ${passwd}.  Print an error and fail if the file
 * is 2048 characters or more, or if it contains any newline \n or \r\n
 * characters other than at the end of the file.  Do not include the \n or
 * \r\n characters in the passphrase.
 */
int readpass_file(char **, const char *);

#endif /* !READPASS_H_ */
