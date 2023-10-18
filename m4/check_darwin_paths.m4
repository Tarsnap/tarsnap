# CHECK_DARWIN_PATHS
# -------------------
AC_DEFUN([CHECK_DARWIN_PATHS],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
*darwin*)
	# Get the homebrew directory, which varies based on arch.
	case "$(uname -m)" in
	arm64)
		homebrew_dir=/opt/homebrew
		;;
	*)
		homebrew_dir=/usr/local
		;;
	esac

	# Use the homebrew directory to specify the paths to openssl.
	CPPFLAGS="${CPPFLAGS} -I${homebrew_dir}/opt/openssl/include"
	LDFLAGS="${LDFLAGS} -L${homebrew_dir}/opt/openssl/lib"
	;;
esac
])# CHECK_DARWIN_PATHS
