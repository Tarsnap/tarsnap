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

/**
 * build_dir(dir, diropt):
 * Makes sure that ${dir} exists, creating it (and any parents) as necessary.
 */
int build_dir(const char *, const char *);

#endif /* !_DIRUTIL_H_ */
