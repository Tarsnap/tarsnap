# CHECK_MEMLIMIT_SUPPORT
# ----------------------
AC_DEFUN([CHECK_MEMLIMIT_SUPPORT], [
	# Check for a linuxy sysinfo syscall; and while we're doing that,
	# check if struct sysinfo is the old version (total RAM == totalmem)
	# or the new version (total RAM == totalmem * mem_unit).
	AC_CHECK_HEADERS_ONCE([sys/sysinfo.h])
	AC_CHECK_FUNCS_ONCE([sysinfo])
	AC_CHECK_TYPES([struct sysinfo], [], [], [[#include <sys/sysinfo.h>]])
	AC_CHECK_MEMBERS([struct sysinfo.totalram, struct sysinfo.mem_unit],
	    [], [], [[#include <sys/sysinfo.h>]])

	# Check if we have <sys/param.h>, since some systems require it for
	# sysctl to work.
	AC_CHECK_HEADERS_ONCE([sys/param.h])

	# Check for <sys/sysctl.h>.  If it exists and it defines HW_USERMEM
	# and/or HW_MEMSIZE, we'll try using those as memory limits.
	AC_CHECK_HEADERS_ONCE([sys/sysctl.h])
])# CHECK_MEMLIMIT_SUPPORT
