# CHECK_BROKEN_TCP_NOPUSH
# -----------------------
AC_DEFUN([CHECK_BROKEN_TCP_NOPUSH],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
darwin*)
	AC_DEFINE([BROKEN_TCP_NOPUSH], [1],
	    [Define to 1 if the OS has a broken TCP_NOPUSH implementation])
	;;
esac

AC_SUBST([BROKEN_TCP_NOPUSH])
])# CHECK_BROKEN_TCP_NOPUSH
