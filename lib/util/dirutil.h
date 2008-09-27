#ifndef _DIRUTIL_H_
#define _DIRUTIL_H_

/**
 * dirutil_fsyncdir(path):
 * Call fsync on the directory ${path}.
 */
int dirutil_fsyncdir(const char *);

/**
 * dirutil_needdir(dirname):
 * Make sure that ${dirname} exists (creating it if necessary) and is a
 * directory.
 */
int dirutil_needdir(const char *);

#endif /* !_DIRUTIL_H_ */
