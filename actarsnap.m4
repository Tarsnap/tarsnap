# CHECK_BROKEN_TCP_NOPUSH
# -----------------------
AC_DEFUN([CHECK_BROKEN_TCP_NOPUSH],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
darwin*)
	AC_DEFINE([BROKEN_TCP_NOPUSH], [1],
	    [Define to 1 if the OS has a broken TCP_NOPUSH implementation])
	;;
cygwin*)
	AC_DEFINE([BROKEN_TCP_NOPUSH], [1],
	    [Define to 1 if the OS has a broken TCP_NOPUSH implementation])
	;;
esac

AC_SUBST([BROKEN_TCP_NOPUSH])
])# CHECK_BROKEN_TCP_NOPUSH

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

# CHECK_LINUX_EXT2FS
# ---------------------------
AC_DEFUN([CHECK_LINUX_EXT2FS],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
linux*)
	AC_CHECK_HEADER(ext2fs/ext2_fs.h,
		AC_DEFINE([HAVE_EXT2FS_EXT2_FS_H], [1],
		    [Define to 1 if you have the <ext2fs/ext2_fs.h> header file.]),
		AC_MSG_ERROR([*** ext2fs/ext2_fs.h missing ***]))
	;;
esac

AC_SUBST([HAVE_EXT2FS_EXT2_FS_H])
])# CHECK_LINUX_EXT2FS

# CHECK_SOLARIS_PATHS
# -------------------
AC_DEFUN([CHECK_SOLARIS_PATHS],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
*solaris* | *sunos*)
	CPPFLAGS="${CPPFLAGS} -I/usr/sfw/include"
	LDFLAGS="${LDFLAGS} -L/usr/sfw/lib -R/usr/sfw/lib"
	LIBS="${LIBS} -lsocket"
	;;
esac
])# CHECK_SOLARIS_PATHS

# CHECK_SYSCTL_HW_USERMEM
# -----------------------
AC_DEFUN([CHECK_SYSCTL_HW_USERMEM],
[if sysctl hw.usermem >/dev/null 2>/dev/null; then
	AC_DEFINE([HAVE_SYSCTL_HW_USERMEM], [1],
	    [Define to 1 if the OS has a hw.usermem sysctl])
fi
AC_SUBST([HAVE_SYSCTL_HW_USERMEM])
])# CHECK_SYSCTL_HW_USERMEM
