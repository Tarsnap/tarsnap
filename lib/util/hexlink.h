#ifndef _HEXLINK_H_
#define _HEXLINK_H_

#include <stddef.h>
#include <stdint.h>

/**
 * hexlink_write(path, buf, buflen):
 * Convert ${buf} (of length ${buflen}) into hexadecimal and create a link
 * from ${path} pointing at it.
 */
int hexlink_write(const char *, const uint8_t *, size_t);

/**
 * hexlink_read(path, buf, buflen):
 * Read the link ${path}, which should point to a hexadecimal string of
 * length 2 * ${buflen}; and parse this into the provided buffer.  In the
 * event of an error, return with errno == ENOENT iff the link does not
 * exist.
 */
int hexlink_read(const char *, uint8_t *, size_t);

#endif /* !_HEXLINK_H_ */
