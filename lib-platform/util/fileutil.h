#ifndef FILEUTIL_H_
#define FILEUTIL_H_

/**
 * fileutil_open_noatime(path, flags, noatime):
 * Act the same as open(2), except that if the OS supports O_NOATIME and
 * ${noatime} is non-zero, attempt to open the path with that set.  If the
 * O_NOATIME attempt fails, do not give any warnings, and attempt a normal
 * open().
 */
int fileutil_open_noatime(const char *, int, int);

#endif /* !FILEUTIL_H_ */
