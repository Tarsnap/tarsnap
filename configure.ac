dnl Process this file with autoconf to produce a configure script.

dnl First, define all of the version numbers up front.
dnl In particular, this allows the version macro to be used in AC_INIT

# Pull in tarsnap version number and server name from files
m4_define([TAR_VERS],m4_include([tar-version]))
m4_define([TS_SERVER],m4_include([tsserver]))

dnl These first two version numbers are updated automatically on each release.
m4_define([LIBARCHIVE_VERSION_S],[2.7.0])
m4_define([LIBARCHIVE_VERSION_N],[2007000])

#
# Now starts the "real" configure script.
#

AC_INIT([tarsnap],[TAR_VERS()],[cperciva@tarsnap.com])
# Make sure the srcdir contains "libarchive" directory
AC_CONFIG_SRCDIR([libarchive])
# Use auxiliary subscripts from this subdirectory (cleans up root)
AC_CONFIG_AUX_DIR([config.aux])
AC_CONFIG_MACRO_DIR([m4])
# Check for host and target type
AC_CANONICAL_TARGET
AC_CANONICAL_HOST
# Must follow AC_CONFIG macros above...
AM_INIT_AUTOMAKE()

# Stick the version numbers into config.h
AC_DEFINE([LIBARCHIVE_VERSION_STRING],"LIBARCHIVE_VERSION_S()",
	[Version number of libarchive])
AC_DEFINE_UNQUOTED([LIBARCHIVE_VERSION_NUMBER],"LIBARCHIVE_VERSION_N()",
	[Version number of libarchive as a single integer])
AC_SUBST([TSSERVER],"TS_SERVER()")

# The shell variables here must be the same as the AC_SUBST() variables
# below, but the shell variable names apparently cannot be the same as
# the m4 macro names above.  Why?  Ask autoconf.
LIBARCHIVE_VERSION_STRING=LIBARCHIVE_VERSION_S()
LIBARCHIVE_VERSION_NUMBER=LIBARCHIVE_VERSION_N()

# Substitute the above version numbers into the various files below.
# Yes, I believe this is the fourth time we define what are essentially
# the same symbols.  Why? Ask autoconf.
AC_SUBST(LIBARCHIVE_VERSION_STRING)
AC_SUBST(LIBARCHIVE_VERSION_NUMBER)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

dnl Compilation on mingw and Cygwin needs special Makefile rules
inc_windows_files=no
inc_cygwin_files=no
case "$host_os" in
  *mingw* ) inc_windows_files=yes ;;
  *cygwin*) inc_cygwin_files=yes ;;
esac
AM_CONDITIONAL([INC_WINDOWS_FILES], [test $inc_windows_files = yes])
AM_CONDITIONAL([INC_CYGWIN_FILES], [test $inc_cygwin_files = yes])

# Checks for programs.
m4_version_prereq([2.70], [AC_PROG_CC], [AC_PROG_CC_C99])
AM_PROG_CC_C_O
AC_PROG_RANLIB
AC_CHECK_TOOL([STRIP],[strip])

# We use some _GNU_SOURCE bits on Linux; this also enables other extensions.
m4_version_prereq([2.70], [AC_USE_SYSTEM_EXTENSIONS], [AC_GNU_SOURCE])

# Don't rebuild the configure script.  I'm distributing a perfectly good one.
AM_MAINTAINER_MODE

# Get the POSIX sh early, in case the configure script causes
# `command -p getconf` to fail in a broken environment.
CHECK_POSIX_SH

#
# Locate some specific link objects for cygwin
#
prog_ldadd_extra=
case "$host_os" in
*cygwin*)
  binmode_path=`$CC -print-file-name=binmode.o`
  if test -n "$binmode_path" && test -f "$binmode_path" ; then
    prog_ldadd_extra="$binmode_path"
  fi
  ;;
esac
PROG_LDADD_EXTRA=$prog_ldadd_extra
AC_SUBST(PROG_LDADD_EXTRA)

# Checks for header files.
m4_version_prereq([2.70], [], [AC_HEADER_STDC])
AC_HEADER_DIRENT
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS_ONCE([acl/libacl.h attr/xattr.h ctype.h err.h errno.h])
AC_CHECK_HEADERS_ONCE([fcntl.h grp.h])
AC_CHECK_HEADERS_ONCE([inttypes.h langinfo.h limits.h linux/fs.h])
AC_CHECK_HEADERS_ONCE([locale.h paths.h poll.h pwd.h regex.h signal.h stdarg.h])
AC_CHECK_HEADERS_ONCE([stdint.h stdlib.h string.h])
AC_CHECK_HEADERS_ONCE([sys/acl.h sys/cdefs.h sys/extattr.h sys/ioctl.h sys/mkdev.h])
AC_CHECK_HEADERS_ONCE([sys/param.h sys/poll.h sys/select.h sys/time.h sys/utime.h])
AC_CHECK_HEADERS_ONCE([time.h unistd.h utime.h wchar.h wctype.h windows.h])

# Check for clock_gettime.  On some systems, this is provided via librt.
AC_CHECK_LIB(rt, clock_gettime)
AC_CHECK_FUNCS_ONCE([clock_gettime])

# Check for <sys/mount.h>, <sys/statvfs.h>, <sys/vfs.h>, and <sys/statfs.h>,
# which are used on various OSes to provide statfs(2) and statvfs(2).
AC_CHECK_HEADERS([sys/mount.h sys/statfs.h sys/statvfs.h sys/vfs.h], [], [],
[#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif])

# Check how we can find out the available memory.
CHECK_MEMLIMIT_SUPPORT

# Check for members of struct statfs and struct statvfs.
AC_CHECK_MEMBERS([struct statfs.f_fstypename, struct statfs.f_type,
    struct statvfs.f_fstypename, struct statvfs.f_basetype], [], [],
[#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif])

# Check for <linux/magic.h>, which contains magic numbers for statfs on
# Linux 2.6.  (We hard-code values for Linux 2.4.)
AC_CHECK_HEADERS_ONCE([linux/magic.h])

# If we're on Solaris, we need to add some extra paths to the Makefile.
# This needs to be done *before* we look for OpenSSL; otherwise we won't
# be able to find it...
CHECK_SOLARIS_PATHS

# Ditto for Darwin.
CHECK_DARWIN_PATHS

# Check if we need -std=c99 in LDFLAGS (for gcc on Solaris).
CHECK_SOLARIS_C99

# Check if we should use mdoc versions of the man pages or versions which
# are uglier but more portable.
CHECK_MDOC_OR_MAN

# On Linux, we need ext2fs/ext2_fs.h
CHECK_LINUX_EXT2FS

# Check if we're on an operating system with a broken TCP_NOPUSH
CHECK_BROKEN_TCP_NOPUSH

# Check if we have FreeBSD's randomized TCP source port bug
CHECK_FREEBSD_PORTRANGE_BUG

# Checks for required libraries.
AC_CHECK_HEADER(zlib.h,
	AC_CHECK_LIB(z,inflate, ,
		AC_MSG_ERROR([*** zlib missing ***])),
	AC_MSG_ERROR([*** zlib header file missing ***]))
AC_CHECK_HEADER(openssl/rsa.h,
	AC_CHECK_LIB(crypto,RSA_private_encrypt, ,
		AC_MSG_ERROR([*** OpenSSL missing ***])),
	AC_MSG_ERROR([*** OpenSSL header files missing ***]))

# Checks for optional libraries.
AC_ARG_WITH([bz2lib],
  AS_HELP_STRING([--without-bz2lib], [Don't build support for bzip2 through bz2lib]))

if test "x$with_bz2lib" != "xno"; then
  AC_CHECK_HEADERS_ONCE([bzlib.h])
  AC_CHECK_LIB(bz2,BZ2_bzDecompressInit)
fi

AC_ARG_WITH([lzmadec],
  AS_HELP_STRING([--without-lzmadec], [Don't build support for lzma through lzmadec]))

if test "x$with_lzmadec" != "xno"; then
  AC_CHECK_HEADERS_ONCE([lzmadec.h])
  AC_CHECK_LIB(lzmadec,lzmadec_decode)
fi

AC_ARG_WITH([lzma],
  AS_HELP_STRING([--without-lzma], [Don't build support for xz through lzma]))

if test "x$with_lzma" != "xno"; then
  AC_CHECK_HEADERS_ONCE([lzma.h])
  AC_CHECK_LIB(lzma,lzma_stream_decoder)
fi

AC_CHECK_HEADERS_ONCE([openssl/md5.h openssl/ripemd.h openssl/sha.h])
AC_CHECK_HEADERS_ONCE([md5.h])
AC_CHECK_HEADERS_ONCE([ripemd.h rmd160.h])
AC_CHECK_HEADERS_ONCE([sha.h sha1.h sha2.h sha256.h])
AC_SEARCH_LIBS(SHA1_Init, crypto ssl md)
AC_CHECK_FUNC(MD5_Init)
if test "x$ac_cv_func_MD5_Init" != "xyes"; then
  AC_CHECK_FUNC(MD5Init,
	[AC_DEFINE(MD5_Init, MD5Init,
	    [Define to the initializes MD5 context function.])
	 AC_DEFINE(MD5_Update, MD5Update,
	    [Define to the updates MD5 context function.])
	 AC_DEFINE(MD5_Final, MD5Final,
	    [Define to the generates final MD5 hash function.])
	])
fi
if test "x$ac_cv_func_MD5_Init" = "xyes" -o "x$ac_cv_func_MD5Init" = "xyes" ; then
  AC_DEFINE(HAVE_MD5, 1, [Define to 1 if you have the `MD5' functions.])
fi
AC_CHECK_FUNC(RIPEMD160_Init)
if test "x$ac_cv_func_RIPEMD160_Init" != "xyes"; then
  AC_CHECK_FUNC(RMD160Init,
	[AC_DEFINE(RIPEMD160_Init, RMD160Init,
	    [Define to the initializes RIPEMD160 context function.])
	 AC_DEFINE(RIPEMD160_Update, RMD160Update,
	    [Define to the updates RIPEMD160 context function.])
	 AC_DEFINE(RIPEMD160_Final, RMD160Final,
	    [Define to the generates final RIPEMD160 hash function.])
	])
fi
if test "x$ac_cv_func_RIPEMD160_Init" = "xyes" -o "x$ac_cv_func_RMD160Init" = "xyes" ; then
  AC_DEFINE(HAVE_RMD160, 1, [Define to 1 if you have the `RIPEMD160' functions.])
fi
AC_CHECK_FUNC(SHA1_Init)
if test "x$ac_cv_func_SHA1_Init" != "xyes"; then
  AC_CHECK_FUNC(SHA1Init,
	[AC_DEFINE(SHA1_Init, SHA1Init,
	    [Define to the initializes SHA1 context function.])
	 AC_DEFINE(SHA1_Update, SHA1Update,
	    [Define to the updates SHA1 context function.])
	 AC_DEFINE(SHA1_Final, SHA1Final,
	    [Define to the generates final SHA1 hash function.])
	])
fi
if test "x$ac_cv_func_SHA1_Init" = "xyes" -o "x$ac_cv_func_SHA1Init" = "xyes" ; then
  AC_DEFINE(HAVE_SHA1, 1, [Define to 1 if you have the `SHA1' functions.])
fi
AC_CHECK_FUNC(SHA256_Init)
if test "x$ac_cv_func_SHA256_Init" = "xyes" ; then
  AC_DEFINE(HAVE_SHA256, 1, [Define to 1 if you have the `SHA256' functions.])
fi
AC_CHECK_FUNC(SHA384_Init)
if test "x$ac_cv_func_SHA384_Init" = "xyes" ; then
  AC_DEFINE(HAVE_SHA384, 1, [Define to 1 if you have the `SHA384' functions.])
fi
AC_CHECK_FUNC(SHA512_Init)
if test "x$ac_cv_func_SHA512_Init" = "xyes" ; then
  AC_DEFINE(HAVE_SHA512, 1, [Define to 1 if you have the `SHA512' functions.])
fi

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

# Check for birthtime in struct stat
AC_CHECK_MEMBERS([struct stat.st_birthtime])

# Check for high-resolution timestamps in struct stat
AC_CHECK_MEMBERS([struct stat.st_birthtimespec.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_mtimespec.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_mtime_n]) # AIX
AC_CHECK_MEMBERS([struct stat.st_umtime]) # Tru64
AC_CHECK_MEMBERS([struct stat.st_mtime_usec]) # Hurd
# Check for block size support in struct stat
AC_CHECK_MEMBERS([struct stat.st_blksize])
# Check for st_flags in struct stat (BSD fflags)
AC_CHECK_MEMBERS([struct stat.st_flags])

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
AC_CHECK_DECLS_ONCE([SIZE_MAX, SSIZE_MAX, INT64_MAX, INT64_MIN, UINT64_MAX, UINT32_MAX])

AC_CHECK_DECL([EFTYPE],
		[AC_DEFINE(HAVE_EFTYPE, 1, [A possible errno value for invalid file format errors])],
		[],
		[#include <errno.h>])
AC_CHECK_DECL([EILSEQ],
		[AC_DEFINE(HAVE_EILSEQ, 1, [A possible errno value for invalid file format errors])],
		[],
		[#include <errno.h>])
AC_CHECK_TYPE([wchar_t],
	        [AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_[]wchar_t), 1, [Define to 1 if the system has the type `wchar_t'.])dnl
		AC_CHECK_SIZEOF([wchar_t])],
		[])

m4_version_prereq([2.70], [], [AC_HEADER_TIME])

# Checks for library functions.
m4_version_prereq([2.72], [], [AC_PROG_GCC_TRADITIONAL])
AC_HEADER_MAJOR
AC_FUNC_FSEEKO
AC_FUNC_MEMCMP
AC_FUNC_LSTAT
AC_FUNC_STAT
AC_FUNC_STRERROR_R
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS_ONCE([chflags chown chroot])
AC_CHECK_FUNCS_ONCE([fchdir fchflags fchmod fchown fcntl fork])
AC_CHECK_FUNCS_ONCE([fstat ftruncate futimes geteuid getpid])
AC_CHECK_FUNCS_ONCE([lchflags lchmod lchown])
AC_CHECK_FUNCS_ONCE([lutimes memmove memset mkdir mkfifo mknod])
AC_CHECK_FUNCS_ONCE([nl_langinfo pipe poll readlink select setenv setlocale])
AC_CHECK_FUNCS_ONCE([strchr strdup strerror strrchr timegm])
AC_CHECK_FUNCS_ONCE([tzset unsetenv utime utimes vfork])
AC_CHECK_FUNCS_ONCE([wcrtomb wcscpy wcslen wctomb wmemcmp wmemcpy])
AC_CHECK_FUNCS_ONCE([lockf posix_memalign qsort_r])

# Check for mmap so we can work around its absence on Minix
AC_CHECK_FUNCS_ONCE([mmap])

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
		[Disable Extended Attributes support (default: check)]))

if test "x$enable_xattr" != "xno"; then
	AC_CHECK_HEADERS_ONCE([attr/xattr.h])
	AC_CHECK_HEADERS_ONCE([sys/xattr.h])
	AC_CHECK_LIB(attr,setxattr)
	AC_CHECK_FUNCS_ONCE([extattr_get_file extattr_list_file])
	AC_CHECK_FUNCS_ONCE([extattr_set_fd extattr_set_file])
	AC_CHECK_FUNCS_ONCE([fsetxattr getxattr])
	AC_CHECK_FUNCS_ONCE([lgetxattr listxattr llistxattr lsetxattr])
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
		[Disable ACL support (default: check)]))

if test "x$enable_acl" != "xno"; then
   AC_CHECK_HEADERS_ONCE([sys/acl.h])
   AC_CHECK_LIB([acl],[acl_get_file])
   AC_CHECK_FUNCS_ONCE([acl_create_entry acl_init acl_set_fd acl_set_fd_np acl_set_file])

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
AC_CHECK_DECLS_ONCE([optarg])
AC_CHECK_DECLS_ONCE([optind])

# Strip the .sample from tarsnap.conf.sample (if requested by the user).
AC_ARG_WITH([conf-no-sample],
    AS_HELP_STRING([--with-conf-no-sample],
        [Install tarsnap.conf without the .sample suffix.]),
    [], [with_conf_no_sample=no])
AM_CONDITIONAL([INSTALL_CONF_NO_SAMPLE],
    [test "x$with_conf_no_sample" != "xno"])

# install bash completion
AC_ARG_WITH([bash-completion-dir],
    AS_HELP_STRING([--with-bash-completion-dir@<:@=DIRNAME@:>@],
        [Install bash completion script. @<:@default=no@:>@]),
    [], [with_bash_completion_dir=no])
# by default, use what the user told us
BASH_COMPLETION_DIR="$with_bash_completion_dir"

# install zsh completion.  Unlike --with-bash-completion-dir, DIRNAME is not
# optional; this script has no mechanism for automatically determining the zsh
# completion directory.
AC_ARG_WITH([zsh-completion-dir],
    AS_HELP_STRING([--with-zsh-completion-dir=DIRNAME],
        [Install zsh completion script. @<:@default=no@:>@]),
    [], [with_zsh_completion_dir=no])
# use what the user told us
ZSH_COMPLETION_DIR="$with_zsh_completion_dir"

# print results
AC_MSG_CHECKING([whether to install bash completion])
AS_IF([test "x$with_bash_completion_dir" != "xno"],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])])

# Check whether the user has requested to disable compiler warnings
CHECK_DISABLE_COMPILER_WARNINGS

# should we set the value automatically?
if test "x$with_bash_completion_dir" = "xyes"; then
    # try to use pkg-config
    AC_CHECK_PROG(HAS_PKG_CONFIG, pkg-config, yes)
    if test "x$HAS_PKG_CONFIG" != "xyes"; then
        AC_MSG_ERROR([Automatic detection of bash-completion directory requires pkg-config.])
    else
        # find the actual value from the system
        PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0],
            [BASH_COMPLETION_DIR="`pkg-config --variable=completionsdir bash-completion`"],
            [BASH_COMPLETION_DIR="/etc/bash_completion.d/";
	     AC_MSG_WARN([Automatic detection of bash-completion directory requires bash-completion >= 2.0; guessing a directory.])])
	AC_MSG_CHECKING([bash completions directory])
	AC_MSG_RESULT([$BASH_COMPLETION_DIR])
    fi
fi

# pass values to Makefile.am
AC_SUBST([BASH_COMPLETION_DIR])
AM_CONDITIONAL([INSTALL_BASH_COMPLETION],[test "x$with_bash_completion_dir" != "xno"])
AC_SUBST([ZSH_COMPLETION_DIR])
AM_CONDITIONAL([INSTALL_ZSH_COMPLETION],[test "x$with_zsh_completion_dir" != "xno"])

# Run the libcperciva POSIX compatibility tests and pass values to the Makefile.
CHECK_LIBCPERCIVA_POSIX([${srcdir}/libcperciva])

AC_OUTPUT
