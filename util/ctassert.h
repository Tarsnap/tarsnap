#ifndef _CTASSERT_H_
#define _CTASSERT_H_

/*
 * CTASSERT(foo) will produce a compile-time error if "foo" is not a constant
 * expression which evaluates to a non-zero value.
 */
#ifndef CTASSERT
#define CTASSERT(x)             _CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)         __CTASSERT(x, y)
#define __CTASSERT(x, y)        typedef char __assert ## y[(x) ? 1 : -1]
#endif

#endif /* !_CTASSERT_H_ */
