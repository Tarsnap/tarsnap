#ifndef _GETFSTYPE_H_
#define _GETFSTYPE_H_

/**
 * getfstype(path):
 * Determine the type of filesystem on which ${path} resides, and return a
 * NUL-terminated malloced string.
 */
char * getfstype(const char *);

/**
 * getfstype_issynthetic(fstype):
 * Return non-zero if the filesystem type ${fstype} is on a list of
 * "synthetic" filesystems (i.e., does not contain normal file data).
 */
int getfstype_issynthetic(const char *);

#endif /* !_GETFSTYPE_H_ */
