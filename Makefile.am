## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign subdir-objects

BUILT_SOURCES = libarchive/archive.h

distclean-local:
	-rm -f tar/getdate.c tar/getdate.h
	-rm -f libarchive/test/list.h
	-rm -rf autom4te.cache/
	-rm -f *~

noinst_LIBRARIES=	libarchive.a
bin_PROGRAMS=	tarsnap tarsnap-keygen tarsnap-keymgmt
dist_man_MANS=$(tarsnap_dist_man_MANS) $(tarsnap_keygen_dist_man_MANS)

#
# Libarchive headers, source, etc.
#
#

libarchive_a_SOURCES=						\
	libarchive/archive_check_magic.c			\
	libarchive/archive_endian.h				\
	libarchive/archive_entry.c				\
	libarchive/archive_entry.h				\
	libarchive/archive_entry_copy_stat.c			\
	libarchive/archive_entry_stat.c				\
	libarchive/archive_entry_strmode.c			\
	libarchive/archive_platform.h				\
	libarchive/archive_private.h				\
	libarchive/archive_read.c				\
	libarchive/archive_read_data_into_fd.c			\
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
	libarchive/archive_write_set_compression_none.c		\
	libarchive/archive_write_set_format.c			\
	libarchive/archive_write_set_format_pax.c		\
	libarchive/archive_write_set_format_ustar.c		\
	libarchive/config_freebsd.h				\
	libarchive/config_windows.h

# archive.h ends up in the build directory, so make sure we can find it.
libarchive_a_CFLAGS=				\
		-Werror				\
		-I$(top_builddir)/libarchive

#
#
# tarsnap source, docs, etc.
#
#

tarsnap_SOURCES=				\
		tar/bsdtar.c			\
		tar/bsdtar.h			\
		tar/bsdtar_platform.h		\
		tar/getdate.c			\
		tar/matching.c			\
		tar/read.c			\
		tar/siginfo.c			\
		tar/tree.c			\
		tar/tree.h			\
		tar/util.c			\
		tar/write.c			\
		glue/archive_multitape.c	\
		glue/tape.c			\
		ccache/ccache_entry.c		\
		ccache/ccache_read.c		\
		ccache/ccache_write.c		\
		multitape/chunkify.c		\
		multitape/multitape_write.c	\
		multitape/multitape_read.c	\
		multitape/multitape_delete.c	\
		multitape/multitape_stats.c	\
		multitape/multitape_fsck.c	\
		multitape/multitape_metadata.c	\
		multitape/multitape_metaindex.c	\
		multitape/multitape_chunkiter.c	\
		multitape/multitape_transaction.c	\
		chunks/chunks_write.c		\
		chunks/chunks_read.c		\
		chunks/chunks_stats.c		\
		chunks/chunks_delete.c		\
		chunks/chunks_transaction.c	\
		chunks/chunks_stats_internal.c	\
		chunks/chunks_directory.c	\
		storage/storage_write.c		\
		storage/storage_read.c		\
		storage/storage_delete.c	\
		storage/storage_directory.c	\
		storage/storage_transaction.c	\
		netpacket/netpacket_transaction.c	\
		netpacket/netpacket_write.c	\
		netpacket/netpacket_read.c	\
		netpacket/netpacket_delete.c	\
		netpacket/netpacket_directory.c	\
		netpacket/netpacket_hmac.c	\
		netpacket/netpacket_op.c	\
		netproto/netproto_connect.c	\
		netproto/netproto.c		\
		netproto/netproto_keyexchange.c	\
		netproto/netproto_packet.c	\
		network/network_select.c	\
		network/network_connect.c	\
		network/network_buf.c		\
		network/network_writeq.c	\
		network/network_cork.c		\
		datastruct/patricia.c		\
		datastruct/rwhashtab.c		\
		crypto/sha256.c			\
		crypto/crypto_entropy.c		\
		crypto/crypto_hash.c		\
		crypto/crypto_keys.c		\
		crypto/crypto_keys_subr.c	\
		crypto/crypto_session.c		\
		crypto/crypto_file.c		\
		crypto/crypto_rsa.c		\
		crypto/crypto_dh.c		\
		crypto/crypto_verify_bytes.c	\
		crypto/crypto_keys_server.c	\
		crypto/crypto_dh_group14.c	\
		util/dirutil.c			\
		util/entropy.c			\
		util/hexify.c			\
		util/hexlink.c			\
		util/sigquit.c

tarsnap_DEPENDENCIES = libarchive.a		\
		libarchive/archive_entry.h	\
		libarchive/archive.h

tarsnap_LDADD= -lcrypto libarchive.a
tarsnap_CFLAGS=					\
		-Werror				\
		-I$(top_builddir)/libarchive	\
		-I$(top_builddir)/tar		\
		-I$(top_builddir)/glue		\
		-I$(top_builddir)/ccache	\
		-I$(top_builddir)/multitape	\
		-I$(top_builddir)/chunks	\
		-I$(top_builddir)/storage	\
		-I$(top_builddir)/netpacket	\
		-I$(top_builddir)/netproto	\
		-I$(top_builddir)/network	\
		-I$(top_builddir)/datastruct	\
		-I${top_builddir}/crypto	\
		-I$(top_builddir)/util		\
		-DETC_TARSNAP_CONF=\"/usr/local/etc/tarsnap.conf\"	\
		-DUSERAGENT=\"tarsnap-${VERSION}\"

tarsnap_dist_man_MANS=	tar/tarsnap.1 tar/tarsnap.conf.5

#
#
# tarsnap-keygen source, docs, etc.
#
#

tarsnap_keygen_SOURCES=				\
		keygen/keygen.c			\
		netpacket/netpacket_register.c	\
		netpacket/netpacket_op.c	\
		netproto/netproto_connect.c	\
		netproto/netproto.c		\
		netproto/netproto_keyexchange.c	\
		netproto/netproto_packet.c	\
		network/network_select.c	\
		network/network_connect.c	\
		network/network_buf.c		\
		network/network_writeq.c	\
		network/network_cork.c		\
		datastruct/rwhashtab.c		\
		crypto/sha256.c			\
		crypto/crypto_entropy.c		\
		crypto/crypto_hash.c		\
		crypto/crypto_keys.c		\
		crypto/crypto_keys_subr.c	\
		crypto/crypto_session.c		\
		crypto/crypto_file.c		\
		crypto/crypto_rsa.c		\
		crypto/crypto_passwd_to_dh.c	\
		crypto/crypto_dh.c		\
		crypto/crypto_verify_bytes.c	\
		crypto/crypto_keys_server.c	\
		crypto/crypto_dh_group14.c	\
		util/entropy.c

tarsnap_keygen_CFLAGS=				\
		-Werror				\
		-I$(top_builddir)/tar		\
		-I${top_builddir}/libarchive	\
		-I$(top_builddir)/netpacket	\
		-I$(top_builddir)/netproto	\
		-I$(top_builddir)/network	\
		-I$(top_builddir)/datastruct	\
		-I${top_builddir}/crypto	\
		-I$(top_builddir)/util		\
		-DUSERAGENT=\"tarsnap-keygen-${VERSION}\"

tarsnap_keygen_LDADD= -lcrypto

tarsnap_keygen_dist_man_MANS=	keygen/tarsnap-keygen.1

#
#
# tarsnap-keymgmt source, docs, etc.
#
#

tarsnap_keymgmt_SOURCES=			\
		keymgmt/keymgmt.c		\
		datastruct/rwhashtab.c		\
		crypto/sha256.c			\
		crypto/crypto_entropy.c		\
		crypto/crypto_hash.c		\
		crypto/crypto_keys.c		\
		crypto/crypto_keys_subr.c	\
		crypto/crypto_session.c		\
		crypto/crypto_file.c		\
		crypto/crypto_rsa.c		\
		crypto/crypto_passwd_to_dh.c	\
		crypto/crypto_dh.c		\
		crypto/crypto_verify_bytes.c	\
		crypto/crypto_keys_server.c	\
		crypto/crypto_dh_group14.c	\
		util/entropy.c

tarsnap_keymgmt_CFLAGS=				\
		-Werror				\
		-I$(top_builddir)/tar		\
		-I${top_builddir}/libarchive	\
		-I$(top_builddir)/datastruct	\
		-I${top_builddir}/crypto	\
		-I$(top_builddir)/util		\
		-DUSERAGENT=\"tarsnap-keygen-${VERSION}\"

tarsnap_keymgmt_LDADD= -lcrypto

tarsnap_keymgmt_dist_man_MANS=	keymgmt/tarsnap-keymgmt.1

#  Tarsnap sample configuration file
sysconf_DATA	=	tar/tarsnap.conf.sample
