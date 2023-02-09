#ifndef _TS_GETFSTYPE_H_
#define _TS_GETFSTYPE_H_

/**
 * ts_getfstype(path):
 * Determine the type of filesystem on which ${path} resides, and return a
 * NUL-terminated malloced string.
 */
char * ts_getfstype(const char *);

/**
 * ts_getfstype_issynthetic(fstype):
 * Return non-zero if the filesystem type ${fstype} is on a list of
 * "synthetic" filesystems (i.e., does not contain normal file data).
 */
int ts_getfstype_issynthetic(const char *);

#endif /* !_TS_GETFSTYPE_H_ */
