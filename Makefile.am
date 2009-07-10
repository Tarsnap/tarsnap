## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS= foreign subdir-objects

#
# What to build and install
#
noinst_LIBRARIES=	libarchive/libarchive.a lib/libtarsnap.a
bin_PROGRAMS=	tarsnap tarsnap-keygen tarsnap-keymgmt
dist_man_MANS=$(tarsnap_dist_man_MANS) $(tarsnap_keygen_dist_man_MANS) $(tarsnap_keymgmt_dist_man_MANS)

#
# Libarchive headers, source, etc.
#
#
libarchive_libarchive_a_SOURCES=						\
	libarchive/archive_check_magic.c			\
	libarchive/archive_endian.h				\
	libarchive/archive_entry.c				\
	libarchive/archive_entry.h				\
	libarchive/archive_entry_copy_stat.c			\
	libarchive/archive_entry_link_resolver.c		\
	libarchive/archive_entry_private.h			\
	libarchive/archive_entry_stat.c				\
	libarchive/archive_entry_strmode.c			\
	libarchive/archive_platform.h				\
	libarchive/archive_private.h				\
	libarchive/archive_read.c				\
	libarchive/archive_read_data_into_fd.c			\
	libarchive/archive_read_disk.c				\
	libarchive/archive_read_disk_entry_from_file.c		\
	libarchive/archive_read_disk_private.h			\
	libarchive/archive_read_disk_set_standard_lookup.c	\
	libarchive/archive_read_extract.c			\
	libarchive/archive_read_open_fd.c			\
	libarchive/archive_read_open_file.c			\
	libarchive/archive_read_open_filename.c			\
	libarchive/archive_read_open_memory.c			\
	libarchive/archive_read_private.h			\
	libarchive/archive_read_support_compression_all.c	\
	libarchive/archive_read_support_compression_bzip2.c	\
	libarchive/archive_read_support_compression_compress.c	\
	libarchive/archive_read_support_compression_gzip.c	\
	libarchive/archive_read_support_compression_none.c	\
	libarchive/archive_read_support_compression_program.c	\
	libarchive/archive_read_support_compression_xz.c	\
	libarchive/archive_read_support_format_all.c		\
	libarchive/archive_read_support_format_ar.c		\
	libarchive/archive_read_support_format_cpio.c		\
	libarchive/archive_read_support_format_empty.c		\
	libarchive/archive_read_support_format_iso9660.c	\
	libarchive/archive_read_support_format_mtree.c		\
	libarchive/archive_read_support_format_tar.c		\
	libarchive/archive_read_support_format_zip.c		\
	libarchive/archive_string.c				\
	libarchive/archive_string.h				\
	libarchive/archive_string_sprintf.c			\
	libarchive/archive_util.c				\
	libarchive/archive_virtual.c				\
	libarchive/archive_write.c				\
	libarchive/archive_write_disk.c				\
	libarchive/archive_write_disk_private.h			\
	libarchive/archive_write_disk_set_standard_lookup.c	\
	libarchive/archive_write_open_fd.c			\
	libarchive/archive_write_open_file.c			\
	libarchive/archive_write_open_filename.c		\
	libarchive/archive_write_open_memory.c			\
	libarchive/archive_write_private.h			\
	libarchive/archive_write_set_compression_bzip2.c	\
	libarchive/archive_write_set_compression_compress.c	\
	libarchive/archive_write_set_compression_gzip.c		\
	libarchive/archive_write_set_compression_none.c		\
	libarchive/archive_write_set_compression_program.c	\
	libarchive/archive_write_set_compression_xz.c		\
	libarchive/archive_write_set_format.c			\
	libarchive/archive_write_set_format_ar.c		\
	libarchive/archive_write_set_format_by_name.c		\
	libarchive/archive_write_set_format_cpio.c		\
	libarchive/archive_write_set_format_cpio_newc.c		\
	libarchive/archive_write_set_format_mtree.c		\
	libarchive/archive_write_set_format_pax.c		\
	libarchive/archive_write_set_format_shar.c		\
	libarchive/archive_write_set_format_ustar.c		\
	libarchive/config_freebsd.h				\
	libarchive/config_windows.h				\
	libarchive/filter_fork.c				\
	libarchive/filter_fork.h

if INC_WINDOWS_FILES
libarchive_libarchive_a_SOURCES+=						\
	libarchive/archive_windows.h				\
	libarchive/archive_windows.c				\
	libarchive/filter_fork_windows.c
endif

libarchive_libarchive_a_CFLAGS=-I $(top_builddir)/libarchive

#
# Tarsnap internal library code
#
#
lib_libtarsnap_a_SOURCES=					\
		lib/crypto/crypto_aesctr.c		\
		lib/crypto/crypto_dh.c			\
		lib/crypto/crypto_dh_group14.c		\
		lib/crypto/crypto_entropy.c		\
		lib/crypto/crypto_file.c		\
		lib/crypto/crypto_hash.c		\
		lib/crypto/crypto_keys.c		\
		lib/crypto/crypto_keys_server.c		\
		lib/crypto/crypto_keys_subr.c		\
		lib/crypto/crypto_passwd_to_dh.c	\
		lib/crypto/crypto_rsa.c			\
		lib/crypto/crypto_scrypt-@SCRYPTVER@.c	\
		lib/crypto/crypto_session.c		\
		lib/crypto/crypto_verify_bytes.c	\
		lib/crypto/sha256.c			\
		lib/datastruct/patricia.c		\
		lib/datastruct/rwhashtab.c		\
		lib/keyfile/keyfile.c			\
		lib/netpacket/netpacket_delete.c	\
		lib/netpacket/netpacket_directory.c	\
		lib/netpacket/netpacket_hmac.c		\
		lib/netpacket/netpacket_op.c		\
		lib/netpacket/netpacket_read.c		\
		lib/netpacket/netpacket_register.c	\
		lib/netpacket/netpacket_transaction.c	\
		lib/netpacket/netpacket_write.c		\
		lib/netproto/netproto.c			\
		lib/netproto/netproto_connect.c		\
		lib/netproto/netproto_keyexchange.c	\
		lib/netproto/netproto_packet.c		\
		lib/network/network_buf.c		\
		lib/network/network_connect.c		\
		lib/network/network_cork.c		\
		lib/network/network_select.c		\
		lib/network/network_writeq.c		\
		lib/scryptenc/scryptenc.c		\
		lib/scryptenc/scryptenc_cpuperf.c	\
		lib/util/asprintf.c			\
		lib/util/b64encode.c			\
		lib/util/dirutil.c			\
		lib/util/entropy.c			\
		lib/util/hexify.c			\
		lib/util/hexlink.c			\
		lib/util/humansize.c			\
		lib/util/memlimit.c			\
		lib/util/readpass.c			\
		lib/util/sigquit.c			\
		lib/util/warnp.c

lib_libtarsnap_a_CFLAGS=					\
		-I$(top_builddir)/tar			\
		-I$(top_builddir)/libarchive		\
		-I${top_builddir}/lib/crypto		\
		-I$(top_builddir)/lib/datastruct	\
		-I$(top_builddir)/lib/keyfile		\
		-I$(top_builddir)/lib/netpacket		\
		-I$(top_builddir)/lib/netproto		\
		-I$(top_builddir)/lib/network		\
		-I$(top_builddir)/lib/scryptenc		\
		-I$(top_builddir)/lib/util

#
#
# tarsnap source, docs, etc.
#
#

tarsnap_SOURCES=			\
		tar/bsdtar.c		\
		tar/bsdtar.h		\
		tar/bsdtar_platform.h	\
		tar/cmdline.c		\
		tar/getdate.c		\
		tar/matching.c		\
		tar/read.c		\
		tar/siginfo.c		\
		tar/subst.c		\
		tar/tree.c		\
		tar/tree.h		\
		tar/util.c		\
		tar/write.c		\
		tar/glue/archive_multitape.c		\
		tar/glue/tape.c				\
		tar/ccache/ccache_entry.c		\
		tar/ccache/ccache_read.c		\
		tar/ccache/ccache_write.c		\
		tar/multitape/chunkify.c		\
		tar/multitape/multitape_write.c		\
		tar/multitape/multitape_read.c		\
		tar/multitape/multitape_delete.c	\
		tar/multitape/multitape_stats.c		\
		tar/multitape/multitape_fsck.c		\
		tar/multitape/multitape_nuke.c		\
		tar/multitape/multitape_metadata.c	\
		tar/multitape/multitape_metaindex.c	\
		tar/multitape/multitape_chunkiter.c	\
		tar/multitape/multitape_transaction.c	\
		tar/chunks/chunks_write.c		\
		tar/chunks/chunks_read.c		\
		tar/chunks/chunks_stats.c		\
		tar/chunks/chunks_delete.c		\
		tar/chunks/chunks_transaction.c		\
		tar/chunks/chunks_stats_internal.c	\
		tar/chunks/chunks_directory.c		\
		tar/storage/storage_write.c		\
		tar/storage/storage_read.c		\
		tar/storage/storage_delete.c		\
		tar/storage/storage_directory.c		\
		tar/storage/storage_transaction.c

if INC_WINDOWS_FILES
tarsnap_SOURCES+=			\
		tar/bsdtar_windows.h	\
		tar/bsdtar_windows.c
endif
if INC_CYGWIN_FILES
tarsnap_SOURCES+=			\
		tar/bsdtar_cygwin.h	\
		tar/bsdtar_cygwin.c
endif

tarsnap_DEPENDENCIES = libarchive/libarchive.a		\
		libarchive/archive_entry.h	\
		libarchive/archive.h		\
		lib/libtarsnap.a

tarsnap_LDADD= -lcrypto libarchive/libarchive.a lib/libtarsnap.a
tarsnap_CFLAGS=						\
		-I$(top_builddir)/libarchive		\
		-DLIBARCHIVE_STATIC			\
		-I$(top_builddir)/tar			\
		-I$(top_builddir)/tar/ccache		\
		-I$(top_builddir)/tar/chunks		\
		-I$(top_builddir)/tar/glue		\
		-I$(top_builddir)/tar/multitape		\
		-I$(top_builddir)/tar/storage		\
		-I${top_builddir}/lib/crypto		\
		-I$(top_builddir)/lib/datastruct	\
		-I$(top_builddir)/lib/keyfile		\
		-I$(top_builddir)/lib/netpacket		\
		-I$(top_builddir)/lib/netproto		\
		-I$(top_builddir)/lib/network		\
		-I$(top_builddir)/lib/util		\
		-DETC_TARSNAP_CONF=\"${sysconfdir}/tarsnap.conf\"	\
		-DUSERAGENT=\"tarsnap-${VERSION}\"
tarsnap_dist_man_MANS=	tar/tarsnap.1 tar/tarsnap.conf.5

#
# tarsnap-keygen source, docs, etc.
#
#

tarsnap_keygen_SOURCES=	keygen/keygen.c

tarsnap_keygen_LDADD= -lcrypto lib/libtarsnap.a
tarsnap_keygen_CFLAGS=					\
		-I${top_builddir}/libarchive		\
		-I$(top_builddir)/tar			\
		-I${top_builddir}/lib/crypto		\
		-I$(top_builddir)/lib/datastruct	\
		-I$(top_builddir)/lib/keyfile		\
		-I$(top_builddir)/lib/netpacket		\
		-I$(top_builddir)/lib/netproto		\
		-I$(top_builddir)/lib/network		\
		-I$(top_builddir)/lib/util		\
		-DUSERAGENT=\"tarsnap-keygen-${VERSION}\"

tarsnap_keygen_dist_man_MANS=	keygen/tarsnap-keygen.1

#
#
# tarsnap-keymgmt source, docs, etc.
#
#

tarsnap_keymgmt_SOURCES= 	keymgmt/keymgmt.c

tarsnap_keymgmt_LDADD= -lcrypto lib/libtarsnap.a
tarsnap_keymgmt_CFLAGS=					\
		-I$(top_builddir)/tar			\
		-I${top_builddir}/libarchive		\
		-I${top_builddir}/lib/crypto		\
		-I$(top_builddir)/lib/datastruct	\
		-I${top_builddir}/lib/keyfile		\
		-I$(top_builddir)/lib/util		\
		-DUSERAGENT=\"tarsnap-keygen-${VERSION}\"

tarsnap_keymgmt_dist_man_MANS=	keymgmt/tarsnap-keymgmt.1

#  Tarsnap sample configuration file
sysconf_DATA	=	tar/tarsnap.conf.sample

# Use the right version of the man pages depending on whether we have mdoc
# macros available on this system.
tar/tarsnap.1:	tar/tarsnap.1-@MANVER@
	cat tar/tarsnap.1-@MANVER@ > tar/tarsnap.1
tar/tarsnap.conf.5: tar/tarsnap.conf.5-@MANVER@
	cat tar/tarsnap.conf.5-@MANVER@ > tar/tarsnap.conf.5
keygen/tarsnap-keygen.1: keygen/tarsnap-keygen.1-@MANVER@
	cat keygen/tarsnap-keygen.1-@MANVER@ > keygen/tarsnap-keygen.1
keymgmt/tarsnap-keymgmt.1: keymgmt/tarsnap-keymgmt.1-@MANVER@
	cat keymgmt/tarsnap-keymgmt.1-@MANVER@ > keymgmt/tarsnap-keymgmt.1
