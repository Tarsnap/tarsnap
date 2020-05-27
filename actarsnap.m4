# CHECK_POSIX_SH
# -------------
AC_DEFUN([CHECK_POSIX_SH], [
	# Get the default value of PATH which is specified to find the
	# standard POSIX utilities.
	POSIX_PATH=`command -p getconf PATH`
	AS_IF([test "x${POSIX_PATH}" = "x"],
	    AC_MSG_ERROR(["cannot get the default PATH"]))
	# Get the path of sh within POSIX_PATH.
	AC_PATH_PROG([POSIX_SH], [sh], [""], [${POSIX_PATH}])
	AS_IF([test "x${POSIX_SH}" = "x"],
	    AC_MSG_ERROR(["cannot find a POSIX shell"]))
])# CHECK_POSIX_SH

# CHECK_LIBCPERCIVA_POSIX
# -----------------------
AC_DEFUN([CHECK_LIBCPERCIVA_POSIX], [
	AC_REQUIRE([CHECK_POSIX_SH])
	AC_MSG_NOTICE([checking POSIX compatibility...])
	LIBCPERCIVA_DIR="$1"
	LDADD_POSIX=`export CC="${CC}"; ${POSIX_SH} ${LIBCPERCIVA_DIR}/POSIX/posix-l.sh "$PATH"`
	CFLAGS_POSIX=`export CC="${CC}"; ${POSIX_SH} ${LIBCPERCIVA_DIR}/POSIX/posix-cflags.sh "$PATH"`
	AC_SUBST([LDADD_POSIX])
	AC_SUBST([CFLAGS_POSIX])
	AC_MSG_RESULT([... done checking POSIX compatibility])
])# CHECK_LIBCPERCIVA_POSIX

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
	LIBS="${LIBS} -lsocket -lnsl"
	;;
esac
])# CHECK_SOLARIS_PATHS

# CHECK_DARWIN_PATHS
# -------------------
AC_DEFUN([CHECK_DARWIN_PATHS],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
*darwin*)
	CPPFLAGS="${CPPFLAGS} -I/usr/local/opt/openssl/include"
	LDFLAGS="${LDFLAGS} -L/usr/local/opt/openssl/lib"
	;;
esac
])# CHECK_DARWIN_PATHS

# CHECK_MDOC_OR_MAN
# -----------------
# If 'nroff -mdoc' or 'mandoc -mdoc' returns with an exit status of 0, we will
# install man pages which use mdoc macros; otherwise, we will install man pages
# which have had mdoc macros stripped out.  On systems which support them, the
# mdoc versions are preferable; but we err on the side of caution -- there may
# be systems where man(1) does not use nroff(1) but still have mdoc macros
# available, yet we will use the mdoc-less man pages on them.
AC_DEFUN([CHECK_MDOC_OR_MAN],
[if nroff -mdoc </dev/null >/dev/null 2>/dev/null; then
	MANVER=mdoc;
elif mandoc -mdoc </dev/null >/dev/null 2>/dev/null; then
	MANVER=mdoc;
else
	MANVER=man;
fi
AC_SUBST([MANVER])
])# CHECK_MDOC_OR_MAN
