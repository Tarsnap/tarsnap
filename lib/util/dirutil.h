#ifndef DIRUTIL_H_
#define DIRUTIL_H_

/**
 * dirutil_fsyncdir(path):
 * Call fsync on the directory ${path}.
 */
int dirutil_fsyncdir(const char *);

/**
 * dirutil_fsync(fp, name):
 * Attempt to write the contents of ${fp} to disk.  Do not close ${fp}.
 *
 * Caveat: "Disks lie" - Kirk McKusick.
 */
int dirutil_fsync(FILE *, const char *);

/**
 * build_dir(dir, diropt):
 * Make sure that ${dir} exists, creating it (and any parents) as necessary.
 */
int build_dir(const char *, const char *);

#endif /* !DIRUTIL_H_ */
