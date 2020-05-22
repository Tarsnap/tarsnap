#ifndef _PARSENUM_H_
#define _PARSENUM_H_

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Handle compiler warnings about implicit variable conversion in PARSENUM. */
#ifdef __clang__

/* Disable clang warnings. */
#define PARSENUM_PROLOGUE						\
_Pragma("clang diagnostic push")					\
_Pragma("clang diagnostic ignored \"-Wunknown-pragmas\"")		\
_Pragma("clang diagnostic ignored \"-Wfloat-conversion\"")		\
_Pragma("clang diagnostic ignored \"-Wsign-conversion\"")		\
_Pragma("clang diagnostic ignored \"-Wshorten-64-to-32\"")		\
_Pragma("clang diagnostic ignored \"-Wconversion\"")

/* Enable clang warnings for code outside of PARSENUM. */
#define PARSENUM_EPILOGUE						\
_Pragma("clang diagnostic pop")

/* Other compilers don't need any special handling */
#else
#define PARSENUM_PROLOGUE /* NOTHING */
#define PARSENUM_EPILOGUE /* NOTHING */
#endif /* !__clang__ */

/* Print a message before assert(). */
#define ASSERT_FAIL(s)							\
	(								\
		fprintf(stderr, "Assertion failed: " s			\
		    ", function %s, file %s, line %d\n",		\
		    __func__, __FILE__, __LINE__),			\
		abort()							\
	)

/**
 * PARSENUM(x, s, min, max):
 * Parse the string ${s} according to the type of the unsigned integer, signed
 * integer, or floating-point number variable ${x}.  If the string consists of
 * optional whitespace followed by a number (and nothing else) and the numeric
 * interpretation of the number is between ${min} and ${max} inclusive, store
 * the value into ${x}, set errno to zero, and return zero.  Otherwise, return
 * nonzero with an unspecified value of ${x} and errno set to EINVAL or ERANGE
 * as appropriate.
 *
 * For floating-point and unsigned integer variables ${x}, this can also be
 * invoked as PARSENUM(x, s), in which case the minimum and maximum values are
 * set to +/- infinity or the limits of the unsigned integer type.
 */
#define PARSENUM2(x, s)							\
	PARSENUM_EX4(x, s, 0, 0, "PARSENUM")
#define PARSENUM4(x, s, min, max)					\
	PARSENUM_EX6(x, s, min, max, 0, 0, "PARSENUM")

/* Magic to select which version of PARSENUM to use. */
#define PARSENUM(...)	PARSENUM_(PARSENUM_COUNT(__VA_ARGS__))(__VA_ARGS__)
#define PARSENUM_(N)	PARSENUM__(N)
#define PARSENUM__(N)	PARSENUM ## N
#define PARSENUM_COUNT(...)	PARSENUM_COUNT_(__VA_ARGS__, 4, 3, 2, 1)
#define PARSENUM_COUNT_(_1, _2, _3, _4, N, ...)	N

/**
 * PARSENUM_EX(x, s, min, max, base, trailing):
 * Parse the string ${s} according to the type of the unsigned integer or
 * signed integer variable ${x}, in the specified ${base}.  If the string
 * consists of optional whitespace followed by a number (and nothing else) and
 * the numeric interpretation of the number is between ${min} and ${max}
 * inclusive, store the value into ${x}, set errno to zero, and return zero.
 * Otherwise, return nonzero with an unspecified value of ${x} and errno set
 * to EINVAL or ERANGE as appropriate.  The check for trailing characters (and
 * EINVAL if they are found) is disabled if ${trailing} is non-zero.
 *
 * For an unsigned integer variable ${x}, this can also be invoked as
 * PARSENUM_EX(x, s, base), in which case the minimum and maximum values are
 * set to the limits of the unsigned integer type.
 *
 * For a floating-point variable ${x}, the ${base} must be 0.
 */
#define PARSENUM_EX4(x, s, base, trailing, _define_name)		\
	(								\
		PARSENUM_PROLOGUE					\
		errno = 0,						\
		(((*(x)) = 1, (*(x)) /= 2) > 0)	?			\
			(((base) == 0) ?				\
				((*(x)) = parsenum_float((s),		\
				    (double)-INFINITY,			\
				    (double)INFINITY, (trailing))) :	\
				(ASSERT_FAIL(_define_name " applied to"	\
				    " float with base != 0"), 1)) :	\
		(((*(x)) = -1) > 0) ?					\
			((*(x)) = parsenum_unsigned((s), 0, (*(x)),	\
			    (*(x)), (base), (trailing))) :		\
			(ASSERT_FAIL(_define_name " applied to signed"	\
			    " integer without specified bounds"), 1),	\
		errno != 0						\
		PARSENUM_EPILOGUE					\
	)
#define PARSENUM_EX6(x, s, min, max, base, trailing, _define_name)	\
	(								\
		PARSENUM_PROLOGUE					\
		errno = 0,						\
		(((*(x)) = 1, (*(x)) /= 2) > 0)	?			\
			(((base) == 0) ?				\
				((*(x)) = parsenum_float((s),		\
				    (double)(min), (double)(max),	\
				    (trailing))) :			\
				(ASSERT_FAIL(_define_name " applied to"	\
				    " float with base != 0"), 1)) :	\
		(((*(x)) = -1) <= 0) ?					\
			((*(x)) = parsenum_signed((s),			\
			    ((*(x)) <= 0) ? (min) : 0,			\
			    ((*(x)) <= 0) ? (max) : 0, (base),		\
			    (trailing))) :				\
			(((*(x)) = parsenum_unsigned((s),		\
			    ((min) <= 0) ? 0 : (min),			\
			    (uintmax_t)(max), (*(x)), (base),		\
			    (trailing))),				\
				((((max) < 0) && (errno == 0)) ?	\
				    (errno = ERANGE) : 0)),		\
		errno != 0						\
		PARSENUM_EPILOGUE					\
	)

/* Magic to select which version of PARSENUM_EX to use. */
#define PARSENUM_EX(...)	PARSENUM_EX_(PARSENUM_EX_COUNT(__VA_ARGS__))(__VA_ARGS__, "PARSENUM_EX")
#define PARSENUM_EX_(N)	PARSENUM_EX__(N)
#define PARSENUM_EX__(N)	PARSENUM_EX ## N
#define PARSENUM_EX_COUNT(...)	PARSENUM_EX_COUNT_(__VA_ARGS__, 6, 5, 4, 3, 2, 1)
#define PARSENUM_EX_COUNT_(_1, _2, _3, _4, _5, _6, N, ...)	N

/* Functions for performing the parsing and parameter checking. */
static inline double
parsenum_float(const char * s, double min, double max, int trailing)
{
	char * eptr;
	double val;

	val = strtod(s, &eptr);
	if (eptr == s || (!trailing && (*eptr != '\0')))
		errno = EINVAL;
	else if ((val < min) || (val > max))
		errno = ERANGE;
	return (val);
}

static inline intmax_t
parsenum_signed(const char * s, intmax_t min, intmax_t max, int base,
    int trailing)
{
	char * eptr;
	intmax_t val;

	val = strtoimax(s, &eptr, base);
	if (eptr == s || (!trailing && (*eptr != '\0')))
		errno = EINVAL;
	else if ((val < min) || (val > max)) {
		errno = ERANGE;
		val = 0;
	}
	return (val);
}

static inline uintmax_t
parsenum_unsigned(const char * s, uintmax_t min, uintmax_t max,
    uintmax_t typemax, int base, int trailing)
{
	char * eptr;
	uintmax_t val;

	val = strtoumax(s, &eptr, base);
	if (eptr == s || (!trailing && (*eptr != '\0')))
		errno = EINVAL;
	else if ((val < min) || (val > max) || (val > typemax))
		errno = ERANGE;
	return (val);
}

#endif /* !_PARSENUM_H_ */
