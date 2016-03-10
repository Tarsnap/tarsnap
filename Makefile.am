## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS= foreign subdir-objects

CFLAGS += -Wno-unused-function

#
# What to build and install
#
noinst_LIBRARIES=	libarchive/libarchive.a lib/libtarsnap.a lib/libtarsnap_aesni.a lib/libtarsnap_sse2.a
bin_PROGRAMS=	tarsnap tarsnap-keygen tarsnap-keyregen tarsnap-recrypt tarsnap-keymgmt
dist_man_MANS=$(tarsnap_dist_man_MANS) $(tarsnap_keygen_dist_man_MANS) $(tarsnap_keyregen_dist_man_MANS) $(tarsnap_recrypt_dist_man_MANS) $(tarsnap_keymgmt_dist_man_MANS)
CLEANFILES=	$(dist_man_MANS)

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
	libarchive/archive_write_set_compression_none.c		\
	libarchive/archive_write_set_format_pax.c		\
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

libarchive_libarchive_a_CPPFLAGS=-I$(top_builddir)/libarchive

#
# Tarsnap internal library code
#
#
lib_libtarsnap_a_SOURCES=					\
		lib/crypto/crypto_file.c			\
		lib/crypto/crypto_hash.c			\
		lib/crypto/crypto_keys.c			\
		lib/crypto/crypto_keys_server.c			\
		lib/crypto/crypto_keys_subr.c			\
		lib/crypto/crypto_passwd_to_dh.c		\
		lib/crypto/crypto_rsa.c				\
		lib/crypto/crypto_scrypt.c			\
		lib/crypto/crypto_scrypt_smix.c			\
		lib/crypto/crypto_session.c			\
		lib/datastruct/patricia.c			\
		lib/datastruct/rwhashtab.c			\
		lib/keyfile/keyfile.c				\
		lib/netpacket/netpacket_delete.c		\
		lib/netpacket/netpacket_directory.c		\
		lib/netpacket/netpacket_hmac.c			\
		lib/netpacket/netpacket_op.c			\
		lib/netpacket/netpacket_read.c			\
		lib/netpacket/netpacket_register.c		\
		lib/netpacket/netpacket_transaction.c		\
		lib/netpacket/netpacket_write.c			\
		lib/netproto/netproto.c				\
		lib/netproto/netproto_connect.c			\
		lib/netproto/netproto_keyexchange.c		\
		lib/netproto/netproto_packet.c			\
		lib/network/tsnetwork_buf.c			\
		lib/network/tsnetwork_bwlimit.c			\
		lib/network/tsnetwork_connect.c			\
		lib/network/tsnetwork_cork.c			\
		lib/network/tsnetwork_register.c		\
		lib/network/tsnetwork_select.c			\
		lib/network/tsnetwork_sleep.c			\
		lib/network/tsnetwork_writeq.c			\
		lib/scryptenc/scryptenc.c			\
		lib/scryptenc/scryptenc_cpuperf.c		\
		lib/util/dirutil.c				\
		lib/util/getfstype.c				\
		lib/util/hexlink.c				\
		lib/util/memlimit.c				\
		lib/util/sigquit.c				\
		lib/util/tvmath.c				\
		libcperciva/alg/sha256.c			\
		libcperciva/cpusupport/cpusupport_x86_aesni.c	\
		libcperciva/cpusupport/cpusupport_x86_sse2.c	\
		libcperciva/crypto/crypto_aes.c			\
		libcperciva/crypto/crypto_aesctr.c		\
		libcperciva/crypto/crypto_dh.c			\
		libcperciva/crypto/crypto_dh_group14.c		\
		libcperciva/crypto/crypto_entropy.c		\
		libcperciva/crypto/crypto_verify_bytes.c	\
		libcperciva/datastruct/elasticarray.c		\
		libcperciva/datastruct/ptrheap.c		\
		libcperciva/datastruct/timerqueue.c		\
		libcperciva/events/events.c			\
		libcperciva/events/events_immediate.c		\
		libcperciva/events/events_network.c		\
		libcperciva/events/events_network_selectstats.c	\
		libcperciva/events/events_timer.c		\
		libcperciva/network/network_accept.c		\
		libcperciva/network/network_connect.c		\
		libcperciva/network/network_read.c		\
		libcperciva/network/network_write.c		\
		libcperciva/util/asprintf.c			\
		libcperciva/util/b64encode.c			\
		libcperciva/util/entropy.c			\
		libcperciva/util/getopt.c			\
		libcperciva/util/hexify.c			\
		libcperciva/util/humansize.c			\
		libcperciva/util/insecure_memzero.c		\
		libcperciva/util/monoclock.c			\
		libcperciva/util/readpass.c			\
		libcperciva/util/sock.c				\
		libcperciva/util/sock_util.c			\
		libcperciva/util/ttyfd.c			\
		libcperciva/util/warnp.c			\
		cpusupport-config.h

lib_libtarsnap_a_CPPFLAGS=				\
		-I${top_srcdir}/tar			\
		-I$(top_srcdir)/libarchive		\
		-I${top_srcdir}/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/scryptenc		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/libcperciva/alg		\
		-I$(top_srcdir)/libcperciva/cpusupport	\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/datastruct	\
		-I$(top_srcdir)/libcperciva/events	\
		-I$(top_srcdir)/libcperciva/network	\
		-I$(top_srcdir)/libcperciva/util	\
		-DCPUSUPPORT_CONFIG_FILE=\"cpusupport-config.h\" \
		-DPOSIXFAIL_MSG_NOSIGNAL \
		-DPOSIXFAIL_CLOCK_REALTIME

LIBTARSNAP_A=	lib/libtarsnap.a

cpusupport-config.h:
	( export CC="${CC}"; export CFLAGS="${CFLAGS}"; command -p sh $(srcdir)/libcperciva/cpusupport/Build/cpusupport.sh ) > cpusupport-config.h.tmp && command -p mv cpusupport-config.h.tmp cpusupport-config.h
BUILT_SOURCES=	cpusupport-config.h
CLEANFILES+=	cpusupport-config.h cpusupport-config.h.tmp

lib_libtarsnap_aesni_a_SOURCES=				\
		libcperciva/crypto/crypto_aes_aesni.c	\
		cpusupport-config.h
lib_libtarsnap_aesni_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_aesni_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_X86_AESNI}`

LIBTARSNAP_A+=	lib/libtarsnap_aesni.a

lib_libtarsnap_sse2_a_SOURCES=				\
		lib/crypto/crypto_scrypt_smix_sse2.c	\
		cpusupport-config.h
lib_libtarsnap_sse2_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_sse2_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_X86_SSE2}`

LIBTARSNAP_A+=	lib/libtarsnap_sse2.a

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
		tar/multitape/multitape_recover.c	\
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
		$(LIBTARSNAP_A)

tarsnap_LDADD= -lcrypto libarchive/libarchive.a $(LIBTARSNAP_A)
tarsnap_CPPFLAGS=					\
		-I$(top_srcdir)/libarchive		\
		-DLIBARCHIVE_STATIC			\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/tar/ccache		\
		-I$(top_srcdir)/tar/chunks		\
		-I$(top_srcdir)/tar/glue		\
		-I$(top_srcdir)/tar/multitape		\
		-I$(top_srcdir)/tar/storage		\
		-I${top_srcdir}/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/datastruct	\
		-I$(top_srcdir)/libcperciva/util	\
		"-DETC_TARSNAP_CONF=\"${sysconfdir}/tarsnap.conf\""	\
		-DUSERAGENT=\"tarsnap-${VERSION}\"
tarsnap_dist_man_MANS=	tar/tarsnap.1 tar/tarsnap.conf.5

#
# tarsnap-keygen source, docs, etc.
#
#

tarsnap_keygen_SOURCES=	keygen/keygen.c			\
		keygen/keygen_actual.c			\
		keygen/keygen_network.c

tarsnap_keygen_LDADD= -lcrypto $(LIBTARSNAP_A)
tarsnap_keygen_CPPFLAGS=				\
		-I${top_srcdir}/libarchive		\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/keygen			\
		-I${top_srcdir}/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util	\
		-DUSERAGENT=\"tarsnap-keygen-${VERSION}\"

tarsnap_keygen_dist_man_MANS=	keygen/tarsnap-keygen.1

#
# tarsnap-keyregen source, docs, etc.
#
#

tarsnap_keyregen_SOURCES=	keyregen/keyregen.c	\
		keygen/keygen_actual.c			\
		keygen/keygen_network.c

tarsnap_keyregen_LDADD= -lcrypto $(LIBTARSNAP_A)
tarsnap_keyregen_CPPFLAGS=				\
		-I${top_srcdir}/libarchive		\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/keygen			\
		-I${top_srcdir}/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util	\
		-DUSERAGENT=\"tarsnap-keyregen-${VERSION}\"

tarsnap_keyregen_dist_man_MANS=	keyregen/tarsnap-keyregen.1

#
#
# tarsnap-recrypt source, docs, etc.
#
#

tarsnap_recrypt_SOURCES=	recrypt/recrypt.c	\
		tar/multitape/multitape_metadata.c	\
		tar/multitape/multitape_transaction.c	\
		tar/chunks/chunks_write.c		\
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

tarsnap_recrypt_LDADD= -lcrypto $(LIBTARSNAP_A)
tarsnap_recrypt_CPPFLAGS=				\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/libarchive		\
		-I$(top_srcdir)/tar/chunks		\
		-I$(top_srcdir)/tar/multitape		\
		-I$(top_srcdir)/tar/storage		\
		-I${top_srcdir}/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util	\
		-DUSERAGENT=\"tarsnap-recrypt-${VERSION}\"
tarsnap_recrypt_dist_man_MANS=	recrypt/tarsnap-recrypt.1

#
#
# tarsnap-keymgmt source, docs, etc.
#
#

tarsnap_keymgmt_SOURCES= 	keymgmt/keymgmt.c

tarsnap_keymgmt_LDADD= -lcrypto $(LIBTARSNAP_A)
tarsnap_keymgmt_CPPFLAGS=				\
		-I$(top_srcdir)/tar			\
		-I${top_srcdir}/libarchive		\
		-I${top_srcdir}/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I${top_srcdir}/lib/keyfile		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util

tarsnap_keymgmt_dist_man_MANS=	keymgmt/tarsnap-keymgmt.1

#  Tarsnap sample configuration file
sysconf_DATA	=	tar/tarsnap.conf.sample

# Use the right version of the man pages depending on whether we have mdoc
# macros available on this system.
tar/tarsnap.1-man tar/tarsnap.1-mdoc \
tar/tarsnap.conf.5-man tar/tarsnap.conf.5-mdoc:
	sed -e 's|@sysconfdir[@]|${sysconfdir}|g' ${srcdir}/$@.in > $@.tmp
	mv $@.tmp $@
tar/tarsnap.1:	tar/tarsnap.1-@MANVER@
	cat tar/tarsnap.1-@MANVER@ > tar/tarsnap.1.tmp
	mv $@.tmp $@
tar/tarsnap.conf.5: tar/tarsnap.conf.5-@MANVER@
	cat tar/tarsnap.conf.5-@MANVER@ > tar/tarsnap.conf.5.tmp
	mv $@.tmp $@
keygen/tarsnap-keygen.1: keygen/tarsnap-keygen.1-@MANVER@
	cat ${top_srcdir}/keygen/tarsnap-keygen.1-@MANVER@ > keygen/tarsnap-keygen.1.tmp
	mv $@.tmp $@
keyregen/tarsnap-keyregen.1: keyregen/tarsnap-keyregen.1-@MANVER@
	cat ${top_srcdir}/keyregen/tarsnap-keyregen.1-@MANVER@ > keyregen/tarsnap-keyregen.1.tmp
	mv $@.tmp $@
recrypt/tarsnap-recrypt.1: recrypt/tarsnap-recrypt.1-@MANVER@
	cat ${top_srcdir}/recrypt/tarsnap-recrypt.1-@MANVER@ > recrypt/tarsnap-recrypt.1.tmp
	mv $@.tmp $@
keymgmt/tarsnap-keymgmt.1: keymgmt/tarsnap-keymgmt.1-@MANVER@
	cat ${top_srcdir}/keymgmt/tarsnap-keymgmt.1-@MANVER@ > keymgmt/tarsnap-keymgmt.1.tmp
	mv $@.tmp $@

CLEANFILES+=   tar/tarsnap.1-mdoc tar/tarsnap.conf.5-mdoc

#
# Misc helper scripts
#
#
if INSTALL_BASH_COMPLETION
bashcompdir = $(BASH_COMPLETION_DIR)
dist_bashcomp_DATA = misc/bash_completion.d/tarsnap \
	misc/bash_completion.d/tarsnap-keygen \
	misc/bash_completion.d/tarsnap-keymgmt \
	misc/bash_completion.d/tarsnap-keyregen \
	misc/bash_completion.d/tarsnap-recrypt
endif

