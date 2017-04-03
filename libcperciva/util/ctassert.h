#ifndef _CTASSERT_H_
#define _CTASSERT_H_

/*
 * CTASSERT(foo) will produce a compile-time error if "foo" is not a constant
 * expression which evaluates to a non-zero value.
 */

/* Kill any existing definition, just in case it's different. */
#ifdef CTASSERT
#undef CTASSERT
#endif

/* Define using libcperciva namespace to avoid collisions. */
#define CTASSERT(x)			libcperciva_CTASSERT(x, __LINE__)
#define libcperciva_CTASSERT(x, y)	libcperciva__CTASSERT(x, y)
#define libcperciva__CTASSERT(x, y)	extern char libcperciva__assert ## y[(x) ? 1 : -1]

#endif /* !_CTASSERT_H_ */
