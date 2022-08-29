#ifndef TTYFD_H_
#define TTYFD_H_

/**
 * ttyfd(void):
 * Attempt to return a file descriptor to the attached terminal.  In order of
 * priority, try to open the terminal device, as returned by ctermid(3); then
 * use standard error, standard input, or standard output if any of them are
 * terminals.
 */
int ttyfd(void);

#endif /* !TTYFD_H_ */
