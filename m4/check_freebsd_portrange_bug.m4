# CHECK_FREEBSD_PORTRANGE_BUG
# ---------------------------
AC_DEFUN([CHECK_FREEBSD_PORTRANGE_BUG],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
freebsd*)
	AC_DEFINE([FREEBSD_PORTRANGE_BUG], [1],
	    [Define to 1 if the OS has FreeBSD's randomized portrange bug])
	;;
esac

AC_SUBST([FREEBSD_PORTRANGE_BUG])
])# CHECK_FREEBSD_PORTRANGE_BUG
