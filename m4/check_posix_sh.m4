# CHECK_POSIX_SH
# -------------
AC_DEFUN([CHECK_POSIX_SH], [
	# Allow users to manually specify the path to a POSIX shell.
	AC_ARG_VAR([POSIX_SH], [path of a POSIX shell])

	AS_IF([test "x${POSIX_SH}" = "x"], [
		# If POSIX_SH is not set, get the default value of PATH which
		# is specified to find the standard POSIX utilities.
		POSIX_PATH=`command -p getconf PATH`
		AS_IF([test "x${POSIX_PATH}" = "x"],
		    AC_MSG_ERROR(["cannot get the default PATH"]))
		# Get the path of sh within POSIX_PATH.
		AC_PATH_PROG([POSIX_SH], [sh], [""], [${POSIX_PATH}])
		AS_IF([test "x${POSIX_SH}" = "x"],
		    AC_MSG_ERROR(["cannot find a POSIX shell"]))
		])
])# CHECK_POSIX_SH
