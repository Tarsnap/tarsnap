#include "bsdtar_platform.h"

#ifdef HAVE_SYS_PARAM_H
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
#endif
#ifdef HAVE_LINUX_MAGIC_H
#include <linux/magic.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "getfstype.h"

/* Linux ugliness starts here. */
#ifndef HAVE_STRUCT_STATFS_F_FSTYPENAME
#ifndef HAVE_STRUCT_STATVFS_F_BASETYPE
#ifndef HAVE_STRUCT_STATVFS_F_FSTYPENAME
#ifdef HAVE_STRUCT_STATFS_F_TYPE
/*
 * Some important filesystem numbers are worth including here in case we
 * don't have <linux/magic.h> or it is deficient.
 */
#ifndef DEVFS_SUPER_MAGIC
#define DEVFS_SUPER_MAGIC	0x1373
#endif
#ifndef DEVPTS_SUPER_MAGIC
#define DEVPTS_SUPER_MAGIC	0x1cd1
#endif
#ifndef SYSFS_MAGIC
#define SYSFS_MAGIC		0x62656572
#endif
#ifndef PROC_SUPER_MAGIC
#define PROC_SUPER_MAGIC	0x9fa0
#endif
#ifndef USBDEVICE_SUPER_MAGIC
#define USBDEVICE_SUPER_MAGIC	0x9fa2
#endif
#ifndef SECURITYFS_MAGIC
#define SECURITYFS_MAGIC	0x73636673
#endif
#ifndef SELINUX_MAGIC
#define SELINUX_MAGIC		0xf97cff8c
#endif

struct ftyperec {
	int f_type;
	const char * typename;
} ftypes[] = {
#ifdef ADFS_SUPER_MAGIC
	{ADFS_SUPER_MAGIC, "adfs"},
#endif
#ifdef AFFS_SUPER_MAGIC
	{AFFS_SUPER_MAGIC, "affs"},
#endif
#ifdef AFS_SUPER_MAGIC
	{AFS_SUPER_MAGIC, "afs"},
#endif
#ifdef AUTOFS_SUPER_MAGIC
	{AUTOFS_SUPER_MAGIC, "autofs"},
#endif
#ifdef CODA_SUPER_MAGIC
	{CODA_SUPER_MAGIC, "coda"},
#endif
#ifdef CGROUP_SUPER_MAGIC
	{CGROUP_SUPER_MAGIC, "cgroup"},
#endif
#ifdef DEBUGFS_SUPER_MAGIC
	{DEBUGFS_SUPER_MAGIC, "debugfs"},
#endif
#ifdef EFS_SUPER_MAGIC
	{EFS_SUPER_MAGIC, "efs"},
#endif
#ifdef EXT2_SUPER_MAGIC
	{EXT2_SUPER_MAGIC, "ext2"},
#endif
#ifdef EXT3_SUPER_MAGIC
	{EXT3_SUPER_MAGIC, "ext3"},
#endif
#ifdef EXT4_SUPER_MAGIC
	{EXT4_SUPER_MAGIC, "ext4"},
#endif
#ifdef HTPFS_SUPER_MAGIC
	{HPFS_SUPER_MAGIC, "hpfs"},
#endif
#ifdef ISOFS_SUPER_MAGIC
	{ISOFS_SUPER_MAGIC, "isofs"},
#endif
#ifdef JFFS2_SUPER_MAGIC
	{JFFS2_SUPER_MAGIC, "jffs2"},
#endif
#ifdef MINIX_SUPER_MAGIC
	{MINIX_SUPER_MAGIC, "minix"},
#endif
#ifdef MINIX_SUPER_MAGIC2
	{MINIX_SUPER_MAGIC2, "minix"},
#endif
#ifdef MINIX2_SUPER_MAGIC
	{MINIX2_SUPER_MAGIC, "minix2"},
#endif
#ifdef MINIX2_SUPER_MAGIC2
	{MINIX2_SUPER_MAGIC2, "minix2"},
#endif
#ifdef MINIX3_SUPER_MAGIC
	{MINIX3_SUPER_MAGIC, "minix3"},
#endif
#ifdef MSDOS_SUPER_MAGIC
	{MSDOS_SUPER_MAGIC, "msdos"},
#endif
#ifdef NCP_SUPER_MAGIC
	{NCP_SUPER_MAGIC, "ncp"},
#endif
#ifdef NFS_SUPER_MAGIC
	{NFS_SUPER_MAGIC, "nfs"},
#endif
#ifdef OPENPROM_SUPER_MAGIC
	{OPENPROM_SUPER_MAGIC, "openprom"},
#endif
#ifdef QNX4_SUPER_MAGIC
	{QNX4_SUPER_MAGIC, "qnx4"},
#endif
#ifdef REISERFS_SUPER_MAGIC
	{REISERFS_SUPER_MAGIC, "reiserfs"},
#endif
#ifdef SMB_SUPER_MAGIC
	{SMB_SUPER_MAGIC, "smb"},
#endif
#ifdef ANON_INODE_FS_MAGIC
	{ANON_INODE_FS_MAGIC, "anon_inode_fs"},
#endif
#ifdef TMPFS_MAGIC
	{TMPFS_MAGIC, "tmpfs"},
#endif
	{DEVFS_SUPER_MAGIC, "devfs"},
	{DEVPTS_SUPER_MAGIC, "devpts"},
	{SYSFS_MAGIC, "sysfs"},
	{PROC_SUPER_MAGIC, "proc"},
	{USBDEVICE_SUPER_MAGIC, "usbdevfs"},
	{SECURITYFS_MAGIC, "securityfs"},
	{SELINUX_MAGIC, "selinux"},
	{0, NULL}
};
#endif /* HAVE_STATFS_F_TYPE */
#endif /* !HAVE_STRUCT_STATVFS_F_FSTYPENAME */
#endif /* !HAVE_STRUCT_STATVFS_F_BASETYPE */
#endif /* !HAVE_STRUCT_STATFS_F_FSTYPENAME */
/* Linux ugliness ends here. */

/* List of names of synthetic filesystem types. */
const char * synthetic_filesystems[] = {
	"devfs",		/* Many OSes */
	"procfs",		/* Many OSes */
	"fdescfs",		/* FreeBSD */
	"linprocfs",		/* Linux emulation on FreeBSD */
	"linsysfs",		/* Linux emulation on FreeBSD */
	"proc",			/* Linux */
	"sysfs",		/* Linux */
	"devpts",		/* Linux */
	"usbdevfs",		/* Linux */
	"securityfs",		/* Linux */
	"selinux",		/* Linux */
	"kernfs",		/* NetBSD */
	"ptyfs",		/* NetBSD */
	"dev",			/* Solaris */
	"ctfs",			/* Solaris */
	"mntfs",		/* Solaris */
	"objfs",		/* Solaris */
	"sharefs",		/* Solaris */
	"fd",			/* Solaris */
	NULL
};

/**
 * getfstype(path):
 * Determine the type of filesystem on which ${path} resides, and return a
 * NUL-terminated malloced string.
 */
char *
getfstype(const char * path)
{
	const char * fstype = "Unknown";
#if defined(HAVE_STRUCT_STATFS_F_FSTYPENAME)
	struct statfs sfs;
#elif defined(HAVE_STRUCT_STATVFS_F_BASETYPE)
	struct statvfs svfs;
#elif defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
	struct statvfs svfs;
#elif defined(HAVE_STRUCT_STATFS_F_TYPE)
	struct statfs sfs;
	size_t i;
#endif

#if defined(HAVE_STRUCT_STATFS_F_FSTYPENAME)
	/* The fs type name is in f_fstypename after we call statfs(2). */
	if (statfs(path, &sfs) == 0)
		fstype = sfs.f_fstypename;
#elif defined(HAVE_STRUCT_STATVFS_F_BASETYPE)
	/* The fs type name is in f_basetype after we call statvfs(2). */
	if (statvfs(path, &svfs) == 0)
		fstype = svfs.f_basetype;
#elif defined(HAVE_STRUCT_STATVFS_F_FSTYPENAME)
	/* The fs type name is in f_fstypename after we call fstatvfs(2). */
	if (statvfs(path, &svfs) == 0)
		fstype = svfs.f_fstypename;
#elif defined(HAVE_STRUCT_STATFS_F_TYPE)
	/* We need to call statfs(2) and interpret f_type values. */
	if (statfs(path, &sfs) == 0) {
		for (i = 0; ftypes[i].typename != NULL; i++) {
			if (sfs.f_type == ftypes[i].f_type) {
				fstype = ftypes[i].typename;
				break;
			}
		}
	}
#endif

	/* Pass back a malloc-allocated string. */
	return (strdup(fstype));
}

/**
 * getfstype_issynthetic(fstype):
 * Return non-zero if the filesystem type ${fstype} is on a list of
 * "synthetic" filesystems (i.e., does not contain normal file data).
 */
int
getfstype_issynthetic(const char * fstype)
{
	size_t i;

	/*
	 * Look through the list of names of synthetic filesystems types, and
	 * stop if we find one which matches the string we're given.
	 */
	for (i = 0; synthetic_filesystems[i] != NULL; i++) {
		if (!strcmp(fstype, synthetic_filesystems[i]))
			break;
	}

	/* If we reached the end, return 0; otherwise, return 1. */
	if (synthetic_filesystems[i] == NULL)
		return (0);
	else
		return (1);
}
