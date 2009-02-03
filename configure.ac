# M4 magic to pull version numbers from separate files
m4_define([LIBARCHIVE_VERSION_S],m4_include([larchive-version-s]))
m4_define([LIBARCHIVE_VERSION_N],m4_include([larchive-version-n]))
m4_define([TAR_VERS],m4_include([tar-version]))
m4_define([TS_SERVER],m4_include([tsserver]))

AC_INIT([tarsnap],TAR_VERS())

#
# Now starts the "real" configure script.
#

# Make sure the srcdir contains "libarchive" directory
AC_CONFIG_SRCDIR([libarchive])
# Use auxiliary subscripts from this subdirectory (cleans up root)
AC_CONFIG_AUX_DIR([config.aux])
# Figure out what OS we're compiling for
AC_CANONICAL_TARGET
# Must follow AC_CONFIG macros above...
AM_INIT_AUTOMAKE()

# Libtool versioning uses different conventions on different
# platforms.  At least on FreeBSD, libtool uses an overly complex
# convention that attempts to solve problems that most people just
# don't have and which just causes confusion for most end users.
ARCHIVE_MAJOR=$(( LIBARCHIVE_VERSION_N() / 1000000 ))
ARCHIVE_MINOR=$(( (LIBARCHIVE_VERSION_N() / 1000) % 1000 ))
ARCHIVE_REVISION=$(( LIBARCHIVE_VERSION_N() % 1000 ))
ARCHIVE_LIBTOOL_MAJOR=`echo $((${ARCHIVE_MAJOR} + ${ARCHIVE_MINOR}))`
ARCHIVE_LIBTOOL_VERSION=$ARCHIVE_LIBTOOL_MAJOR:$ARCHIVE_REVISION:$ARCHIVE_MINOR

# Stick the version numbers into config.h
AC_DEFINE([LIBARCHIVE_VERSION_STRING],"LIBARCHIVE_VERSION_S()",
	[Version number of libarchive])
AC_DEFINE_UNQUOTED([LIBARCHIVE_VERSION_NUMBER],"LIBARCHIVE_VERSION_N()",
	[Version number of libarchive as a single integer])
AC_DEFINE([TARVERSION],"TAR_VERS()",[tarsnap version])
AC_DEFINE([TSSERVER],"TS_SERVER()",[tarsnap server name])

# The shell variables here must be the same as the AC_SUBST() variables
# below, but the shell variable names apparently cannot be the same as
# the m4 macro names above.  Why?  Ask autoconf.
LIBARCHIVE_VERSION_STRING="LIBARCHIVE_VERSION_S()"
LIBARCHIVE_VERSION_NUMBER=LIBARCHIVE_VERSION_N()

# Substitute the above version numbers into the various files below.
# Yes, I believe this is the fourth time we define what are essentially
# the same symbols.  Why? Ask autoconf.
AC_SUBST(LIBARCHIVE_VERSION_STRING)
AC_SUBST(LIBARCHIVE_VERSION_NUMBER)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([tar/tarsnap.1])
AC_CONFIG_FILES([tar/tarsnap.conf.5])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_RANLIB
AC_CHECK_TOOL([STRIP],[strip])

# We use asprintf, which is hidden behind _GNU_SOURCE on linux.
AC_GNU_SOURCE

# Don't rebuild the configure script.  I'm distributing a perfectly good one.
AM_MAINTAINER_MODE

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_DIRENT
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([bzlib.h errno.h fcntl.h  grp.h])
AC_CHECK_HEADERS([inttypes.h langinfo.h limits.h])
# Include inttypes.h into archive.h only if appropriate.
# We can't use the HAVE_INTTYPES_H macro here because archive.h
# gets installed as a system header and then included into client code
# that doesn't define such macros.  (And we should certainly not
# define such macros in system headers!)
# Hence the following:
AC_CHECK_HEADER(inttypes.h,
	 [AC_SUBST(ARCHIVE_H_INCLUDE_INTTYPES_H,['#include <inttypes.h> /* For int64_t */'])],
	 [AC_SUBST(ARCHIVE_H_INCLUDE_INTTYPES_H,[''])])
AC_CHECK_HEADERS([locale.h paths.h poll.h pwd.h stdarg.h])
AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/acl.h sys/endian.h sys/ioctl.h])
AC_CHECK_HEADERS([sys/param.h sys/poll.h sys/select.h sys/time.h sys/utime.h])
AC_CHECK_HEADERS([time.h unistd.h utime.h wchar.h])

# If we're on Solaris, we need to add some extra paths to the Makefile.
# This needs to be done *before* we look for OpenSSL; otherwise we won't
# be able to find it...
CHECK_SOLARIS_PATHS

# Checks for libraries.  While libbz2 is optional (if it is not present,
# support for @archive.bz2 will be lost, but that's all), libz and
# libcrypto are necesasry.
AC_CHECK_LIB(bz2,BZ2_bzDecompressInit)
AC_CHECK_HEADER(zlib.h,
	AC_CHECK_LIB(z,inflate, ,
		AC_MSG_ERROR([*** zlib missing ***])),
	AC_MSG_ERROR([*** zlib header file missing ***]))
AC_CHECK_HEADER(openssl/rsa.h,
	AC_CHECK_LIB(crypto,RSA_private_encrypt, ,
		AC_MSG_ERROR([*** OpenSSL missing ***])),
	AC_MSG_ERROR([*** OpenSSL header files missing ***]))

# On Linux, we need ext2fs/ext2_fs.h
CHECK_LINUX_EXT2FS

# Check if we're on an operating system with a broken TCP_NOPUSH
CHECK_BROKEN_TCP_NOPUSH

# Check if we have FreeBSD's randomized TCP source port bug
CHECK_FREEBSD_PORTRANGE_BUG

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_UID_T
AC_TYPE_MODE_T
# AC_TYPE_OFF_T defaults to "long", which limits us to 4GB files on
# most systems... default to "long long" instead.
AC_CHECK_TYPE(off_t, [long long])
AC_TYPE_SIZE_T
AC_CHECK_TYPE(id_t, [unsigned long])
AC_CHECK_TYPE(uintptr_t, [unsigned int])
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_CHECK_MEMBERS([struct tm.tm_gmtoff])
# Check for high-resolution timestamps in struct stat
AC_CHECK_MEMBERS([struct stat.st_mtimespec.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec])

# If you have uintmax_t, we assume printf supports %ju
# If you have unsigned long long, we assume printf supports %llu
# TODO: Check for %ju and %llu support directly.
AC_CHECK_TYPES([uintmax_t, unsigned long long])

# We need int64_t, uint64_t, intmax_t, and uintmax_t
AC_TYPE_INTMAX_T
AC_TYPE_INT64_T
AC_TYPE_UINTMAX_T
AC_TYPE_UINT64_T

# TODO: If any of these are missing, define them right here.
AC_CHECK_DECLS([SIZE_MAX, INT64_MAX, INT64_MIN, UINT64_MAX, UINT32_MAX])

AC_CHECK_DECL([EFTYPE],
		[AC_DEFINE(HAVE_EFTYPE, 1, [A possible errno value for invalid file format errors])],
		[],
		[#include <errno.h>])
AC_CHECK_DECL([EILSEQ],
		[AC_DEFINE(HAVE_EILSEQ, 1, [A possible errno value for invalid file format errors])],
		[],
		[#include <errno.h>])
AC_HEADER_TIME

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_HEADER_MAJOR
AC_FUNC_FSEEKO
AC_FUNC_MEMCMP
AC_FUNC_LSTAT
AC_FUNC_STAT
AC_FUNC_STRERROR_R
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([asprintf chflags chown chroot])
AC_CHECK_FUNCS([fchdir fchflags fchmod fchown fstat])
AC_CHECK_FUNCS([ftruncate futimes getaddrinfo geteuid getopt_long getpid])
AC_CHECK_FUNCS([lchflags lchmod lchown lockf])
AC_CHECK_FUNCS([lutimes memmove memset mkdir mkfifo mknod])
AC_CHECK_FUNCS([nl_langinfo poll qsort_r select setenv setlocale])
AC_CHECK_FUNCS([strchr strdup strerror strrchr timegm])
AC_CHECK_FUNCS([tzset unsetenv utime utimes])
AC_CHECK_FUNCS([wcscpy wcslen wmemcmp wmemcpy])

# FreeBSD's nl_langinfo supports an option to specify whether the
# current locale uses month/day or day/month ordering.  It makes the
# output a little prettier...
AC_CHECK_DECL([D_MD_ORDER],
[AC_DEFINE(HAVE_D_MD_ORDER, 1, [Define to 1 if nl_langinfo supports D_MD_ORDER])],
[],
[#if HAVE_LANGINFO_H
#include <langinfo.h>
#endif
])

# If fnmatch() exists and supports FNM_LEADING_DIR, then bsdtar uses that,
# otherwise it uses some usable (but less featureful) built-in code for
# filename pattern matching.
AC_FUNC_FNMATCH
AC_CHECK_DECL([FNM_LEADING_DIR],
[AC_DEFINE(HAVE_FNM_LEADING_DIR, 1, [Define to 1 if fnmatch(3) supports the FNM_LEADING_DIR flag])],
[],
[#if HAVE_FNMATCH
#define _GNU_SOURCE /* Required on Linux to get GNU extensions */
#include <fnmatch.h>
#endif
])

# Check for dirent.d_namlen field explicitly
# (This is a bit more straightforward than, if not quite as portable as,
# the recipe given by the autoconf maintainers.)
AC_CHECK_MEMBER(struct dirent.d_namlen,,,
[#if HAVE_DIRENT_H
#include <dirent.h>
#endif
])

# Check for Extended Attributes support
AC_ARG_ENABLE([xattr],
		AS_HELP_STRING([--disable-xattr],
		[Enable Extended Attributes support (default: check)]))

if test "x$enable_xattr" != "xno"; then
	AC_CHECK_HEADERS([attr/xattr.h])
	AC_CHECK_LIB(attr,setxattr)
	AC_CHECK_FUNCS([getxattr lgetxattr listxattr llistxattr fsetxattr lsetxattr])
fi

# Check for ACL support
#
# The ACL support in libarchive is written against the POSIX1e draft,
# which was never officially approved and varies quite a bit across
# platforms.  Worse, some systems have completely non-POSIX acl functions,
# which makes the following checks rather more complex than I would like.
#
AC_ARG_ENABLE([acl],
		AS_HELP_STRING([--disable-acl],
		[Enable ACL support (default: check)]))

if test "x$enable_acl" != "xno"; then
   AC_CHECK_HEADERS([sys/acl.h])
   AC_CHECK_LIB([acl],[acl_get_file])
   AC_CHECK_FUNCS([acl_create_entry acl_init acl_set_fd acl_set_fd_np acl_set_file])

   AC_CHECK_TYPES(acl_permset_t,,,
	[#if HAVE_SYS_TYPES_H
	#include <sys/types.h>
	#endif
	#if HAVE_SYS_ACL_H
	#include <sys/acl.h>
	#endif
	])

    # The "acl_get_perm()" function was omitted from the POSIX draft.
    # (It's a pretty obvious oversight; otherwise, there's no way to
    # test for specific permissions in a permset.)  Linux uses the obvious
    # name, FreeBSD adds _np to mark it as "non-Posix extension."
    # Test for both as a double-check that we really have POSIX-style ACL support.
    AC_CHECK_FUNCS(acl_get_perm_np acl_get_perm,,,
	[#if HAVE_SYS_TYPES_H
	#include <sys/types.h>
	#endif
	#if HAVE_SYS_ACL_H
	#include <sys/acl.h>
	#endif
	])

    # MacOS has an acl.h that isn't POSIX.  It can be detected by
    # checking for ACL_USER
    AC_CHECK_DECL([ACL_USER],
		[AC_DEFINE(HAVE_ACL_USER, 1, [True for systems with POSIX ACL support])],
		[],
		[#include <sys/acl.h>])
fi

# Additional requirements
AC_SYS_LARGEFILE

# Interix doesn't provide optarg and optind
AC_CHECK_DECLS([optarg])
AC_CHECK_DECLS([optind])

AC_OUTPUT
