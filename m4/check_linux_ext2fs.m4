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
