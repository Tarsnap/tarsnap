#ifndef GETOPT_H_
#define GETOPT_H_

#include <assert.h>
#include <setjmp.h>
#include <stddef.h>

/**
 * This getopt implementation parses options of the following forms:
 * -a -b -c foo		(single-character options)
 * -abc foo		(packed single-character options)
 * -abcfoo		(packed single-character options and an argument)
 * --foo bar		(long option)
 * --foo=bar		(long option and argument separated by '=')
 *
 * It does not support abbreviated options (e.g., interpreting --foo as
 * --foobar when there are no other --foo* options) since that misfeature
 * results in breakage when new options are added.  It also does not support
 * options appearing after non-options (e.g., "cp foo bar -R") since that is
 * a horrible GNU perversion.
 *
 * Upon encountering '--', it consumes that argument (by incrementing optind)
 * and returns NULL to signal the end of option processing.  Upon encountering
 * a bare '-' argument or any argument not starting with '-' it returns NULL
 * to signal the end of option processing (without consuming the argument).
 * Note that these behaviours do not apply when such strings are encountered
 * as arguments to options; e.g., if "--foo" takes an argument, then the
 * command line arguments "--foo -- --bar" is interpreted as having two
 * options ("--foo --" and "--bar") and no left-over arguments.
 */

/* Work around LLVM bug. */
#ifdef __clang__
#warning Working around bug in LLVM optimizer
#warning For more details see https://bugs.llvm.org/show_bug.cgi?id=27190
#define GETOPT_USE_COMPUTED_GOTO
#endif

/* Work around broken <setjmp.h> header on Solaris. */
#if defined(__GNUC__) && (defined(sun) || defined(__sun))
#warning Working around broken <setjmp.h> header on Solaris
#define GETOPT_USE_COMPUTED_GOTO
#endif

/* Select the method of performing local jumps. */
#ifdef GETOPT_USE_COMPUTED_GOTO
/* Workaround with computed goto. */
#define DO_SETJMP DO_SETJMP_(__LINE__)
#define DO_SETJMP_(x) DO_SETJMP__(x)
#define DO_SETJMP__(x)					\
	void * getopt_initloop = && getopt_initloop_ ## x;		\
	getopt_initloop_ ## x:
#define DO_LONGJMP							\
	goto *getopt_initloop
#else
/* Intended code, for fully C99-compliant systems. */
#define DO_SETJMP							\
	sigjmp_buf getopt_initloop;					\
	if (!getopt_initialized)					\
		sigsetjmp(getopt_initloop, 0)
#define DO_LONGJMP							\
	siglongjmp(getopt_initloop, 1)
#endif

/* Avoid namespace collisions with libc getopt. */
#define getopt	libcperciva_getopt
#define optarg	libcperciva_optarg
#define optind	libcperciva_optind
#define opterr	libcperciva_opterr
#define optreset	libcperciva_optreset

/* Standard getopt global variables. */
extern const char * optarg;
extern int optind, opterr, optreset;

/* Dummy option string, equal to "(dummy)". */
#define GETOPT_DUMMY getopt_dummy

/**
 * GETOPT(argc, argv):
 * When called for the first time (or the first time after optreset is set to
 * a nonzero value), return GETOPT_DUMMY, aka. "(dummy)".  Thereafter, return
 * the next option string and set optarg / optind appropriately; abort if not
 * properly initialized when not being called for the first time.
 */
#define GETOPT(argc, argv) getopt(argc, argv)

/**
 * GETOPT_SWITCH(ch):
 * Jump to the appropriate GETOPT_OPT, GETOPT_OPTARG, GETOPT_MISSING_ARG, or
 * GETOPT_DEFAULT based on the option string ${ch}.  When called for the first
 * time, perform magic to index the options.
 *
 * GETOPT_SWITCH(ch) is equivalent to "switch (ch)" in a standard getopt loop.
 */
#define GETOPT_SWITCH(ch)						\
	volatile size_t getopt_ln_min = __LINE__;			\
	volatile size_t getopt_ln = getopt_ln_min - 1;			\
	volatile int getopt_default_missing = 0;			\
	DO_SETJMP;							\
	switch (getopt_initialized ? getopt_lookup(ch) + getopt_ln_min : getopt_ln++)

/**
 * GETOPT_OPT(os):
 * Jump to this point when the option string ${os} is passed to GETOPT_SWITCH.
 *
 * GETOPT_OPT("-x") is equivalent to "case 'x'" in a standard getopt loop
 * which has an optstring containing "x".
 */
#define GETOPT_OPT(os)	GETOPT_OPT_(os, __LINE__)
#define GETOPT_OPT_(os, ln)	GETOPT_OPT__(os, ln)
#define GETOPT_OPT__(os, ln)						\
	case ln:							\
		if (getopt_initialized)					\
			goto getopt_skip_ ## ln;			\
		getopt_register_opt(os, ln - getopt_ln_min, 0);		\
		DO_LONGJMP;						\
	getopt_skip_ ## ln

/**
 * GETOPT_OPTARG(os):
 * Jump to this point when the option string ${os} is passed to GETOPT_SWITCH,
 * unless no argument is available, in which case jump to GETOPT_MISSING_ARG
 * (if present) or GETOPT_DEFAULT (if not).
 *
 * GETOPT_OPTARG("-x") is equivalent to "case 'x'" in a standard getopt loop
 * which has an optstring containing "x:".
 */
#define GETOPT_OPTARG(os)	GETOPT_OPTARG_(os, __LINE__)
#define GETOPT_OPTARG_(os, ln)	GETOPT_OPTARG__(os, ln)
#define GETOPT_OPTARG__(os, ln)						\
	case ln:							\
		if (getopt_initialized) {				\
			assert(optarg != NULL);				\
			goto getopt_skip_ ## ln;			\
		}							\
		getopt_register_opt(os, ln - getopt_ln_min, 1);		\
		DO_LONGJMP;						\
	getopt_skip_ ## ln

/**
 * GETOPT_MISSING_ARG:
 * Jump to this point if an option string specified in GETOPT_OPTARG is seen
 * but no argument is available.
 *
 * GETOPT_MISSING_ARG is equivalent to "case ':'" in a standard getopt loop
 * which has an optstring starting with ":".  As such, it also has the effect
 * of disabling warnings about invalid options, as if opterr had been zeroed.
 */
#define GETOPT_MISSING_ARG	GETOPT_MISSING_ARG_(__LINE__)
#define GETOPT_MISSING_ARG_(ln)	GETOPT_MISSING_ARG__(ln)
#define GETOPT_MISSING_ARG__(ln)					\
	case ln:							\
		if (getopt_initialized)					\
			goto getopt_skip_ ## ln;			\
		getopt_register_missing(ln - getopt_ln_min);		\
		DO_LONGJMP;						\
	getopt_skip_ ## ln

/**
 * GETOPT_DEFAULT:
 * Jump to this point if an unrecognized option is seen or if an option
 * specified in GETOPT_OPTARG is seen, no argument is available, and there is
 * no GETOPT_MISSING_ARG label.
 *
 * GETOPT_DEFAULT is equivalent to "case '?'" in a standard getopt loop.
 *
 * NOTE: This MUST be present in the GETOPT_SWITCH statement, and MUST occur
 * after all other GETOPT_* labels.
 */
#define GETOPT_DEFAULT		GETOPT_DEFAULT_(__LINE__)
#define GETOPT_DEFAULT_(ln)	GETOPT_DEFAULT__(ln)
#define GETOPT_DEFAULT__(ln)						\
		goto getopt_skip_ ## ln;				\
	case ln:							\
		getopt_initialized = 1;					\
		break;							\
	default:							\
		if (getopt_initialized)					\
			goto getopt_skip_ ## ln;			\
		if (!getopt_default_missing) {				\
			getopt_setrange(ln - getopt_ln_min);		\
			getopt_default_missing = 1;			\
		}							\
		DO_LONGJMP;						\
	getopt_skip_ ## ln

/*
 * The back-end implementation.  These should be considered internal
 * interfaces and not used directly.
 */
const char * getopt(int, char * const []);
size_t getopt_lookup(const char *);
void getopt_register_opt(const char *, size_t, int);
void getopt_register_missing(size_t);
void getopt_setrange(size_t);
extern const char * getopt_dummy;
extern int getopt_initialized;

#endif /* !GETOPT_H_ */
