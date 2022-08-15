# CHECK_DISABLE_COMPILER_WARNINGS
# -------------------------------
AC_DEFUN([CHECK_DISABLE_COMPILER_WARNINGS], [
	AC_MSG_CHECKING([compiler_warnings])
	AC_ARG_ENABLE(compiler_warnings,
	   AS_HELP_STRING([--disable-compiler-warnings],
	       [Do not request compiler warnings. @<:@default=enabled@:>@]),
	   [ac_compiler_warnings=$enableval],
	   [ac_compiler_warnings=yes])
	AC_MSG_RESULT([${ac_compiler_warnings}])
	AS_IF([test x${ac_compiler_warnings} = xyes],
	   [AX_CFLAGS_WARN_ALL])
])# CHECK_DISABLE_COMPILER_WARNINGS
