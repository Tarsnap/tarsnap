# CHECK_SOLARIS_C99
# ----------------
# On Solaris, the default standard library is c89-compatible.  Some linkers
# require -std=c99 to link to the c99-compatible library.
AC_DEFUN([CHECK_SOLARIS_C99],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
*solaris* | *sunos*)
	AC_MSG_CHECKING([Solaris c99 standard library])
	AC_RUN_IFELSE([AC_LANG_SOURCE([[#include <stdlib.h>
		int main(void) {
			char * eptr;
			strtod("0x1", &eptr);
			return (eptr[0] != '\0');
		}]])],
		[AC_MSG_RESULT([yes])],
		[# If we failed, try adding -std=c99 to the LDFLAGS.
		 LDFLAGS="${LDFLAGS} -std=c99"
		 AC_RUN_IFELSE([AC_LANG_SOURCE([[#include <stdlib.h>
			int main(void) {
				char * eptr;
				strtod("0x1", &eptr);
				return (eptr[0] != '\0');
			}]])],
			[AC_MSG_RESULT([yes, if linked with -std=c99])],
			[AC_MSG_RESULT([no])
			 AC_MSG_ERROR([c99 required])],
			)],
		[AC_MSG_RESULT([skipping due to cross-compiling])])
	;;
*)
	;;
esac

])# CHECK_SOLARIS_C99
