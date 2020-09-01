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
