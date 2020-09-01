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
