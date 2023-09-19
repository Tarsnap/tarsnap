# CHECK_DARWIN_PATHS
# -------------------
AC_DEFUN([CHECK_DARWIN_PATHS],
[AC_REQUIRE([AC_CANONICAL_TARGET])

case $target_os in
*darwin*)
  case "$(uname -m)" in
    arm64)
      CPPFLAGS="${CPPFLAGS} -I/opt/homebrew/opt/openssl/include"
      LDFLAGS="${LDFLAGS} -L/opt/homebrew/opt/openssl/lib"
      ;;
    **)
      CPPFLAGS="${CPPFLAGS} -I/usr/local/opt/openssl/include"
      LDFLAGS="${LDFLAGS} -L/usr/local/opt/openssl/lib"
      ;;
  esac
  ;;
esac
])# CHECK_DARWIN_PATHS
