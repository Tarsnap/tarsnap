## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS= foreign subdir-objects

#
# What to build and install
#
noinst_LIBRARIES=	libarchive/libarchive.a lib/libtarsnap.a	\
			lib/libtarsnap_aesni.a lib/libtarsnap_rdrand.a	\
			lib/libtarsnap_shani.a lib/libtarsnap_sse2.a	\
			lib/libtarsnap_arm_aes.a lib/libtarsnap_arm_sha256.a
bin_PROGRAMS=	tarsnap tarsnap-keygen tarsnap-keyregen tarsnap-recrypt tarsnap-keymgmt
noinst_PROGRAMS=	tests/valgrind/potential-memleaks
man_MANS=$(tarsnap_man_MANS) $(tarsnap_keygen_man_MANS) $(tarsnap_keyregen_man_MANS) $(tarsnap_recrypt_man_MANS) $(tarsnap_keymgmt_man_MANS)
CLEANFILES=	$(man_MANS)

#
# Libarchive headers, source, etc.
#
#
libarchive_libarchive_a_SOURCES=				\
	libarchive/archive.h					\
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
libarchive_libarchive_a_SOURCES+=				\
	libarchive/archive_windows.h				\
	libarchive/archive_windows.c				\
	libarchive/filter_fork_windows.c
endif

libarchive_libarchive_a_CPPFLAGS=-I$(top_builddir)/libarchive

#
# Tarsnap internal library code
#
#
# Don't include crypto_aesctr_shared.c in this list, as it should be included
# directly into other translation unit(s), and not compiled on its own.
lib_libtarsnap_a_SOURCES=					\
		lib/crypto/crypto.h				\
		lib/crypto/crypto_compat.c			\
		lib/crypto/crypto_compat.h			\
		lib/crypto/crypto_file.c			\
		lib/crypto/crypto_hash.c			\
		lib/crypto/crypto_internal.h			\
		lib/crypto/crypto_keys.c			\
		lib/crypto/crypto_keys_server.c			\
		lib/crypto/crypto_keys_subr.c			\
		lib/crypto/crypto_passwd_to_dh.c		\
		lib/crypto/crypto_rsa.c				\
		lib/crypto/crypto_scrypt_smix.c			\
		lib/crypto/crypto_scrypt_smix.h			\
		lib/crypto/crypto_scrypt_smix_sse2.h		\
		lib/crypto/crypto_session.c			\
		lib/datastruct/patricia.c			\
		lib/datastruct/patricia.h			\
		lib/datastruct/rwhashtab.c			\
		lib/datastruct/rwhashtab.h			\
		lib/keyfile/keyfile.c				\
		lib/keyfile/keyfile.h				\
		lib/netpacket/netpacket.h			\
		lib/netpacket/netpacket_delete.c		\
		lib/netpacket/netpacket_directory.c		\
		lib/netpacket/netpacket_hmac.c			\
		lib/netpacket/netpacket_internal.h		\
		lib/netpacket/netpacket_op.c			\
		lib/netpacket/netpacket_read.c			\
		lib/netpacket/netpacket_register.c		\
		lib/netpacket/netpacket_transaction.c		\
		lib/netpacket/netpacket_write.c			\
		lib/netproto/netproto.c				\
		lib/netproto/netproto.h				\
		lib/netproto/netproto_connect.c			\
		lib/netproto/netproto_internal.h		\
		lib/netproto/netproto_keyexchange.c		\
		lib/netproto/netproto_packet.c			\
		lib/network/tsnetwork.h				\
		lib/network/tsnetwork_buf.c			\
		lib/network/tsnetwork_bwlimit.c			\
		lib/network/tsnetwork_connect.c			\
		lib/network/tsnetwork_internal.h		\
		lib/network/tsnetwork_register.c		\
		lib/network/tsnetwork_select.c			\
		lib/network/tsnetwork_sleep.c			\
		lib/network/tsnetwork_writeq.c			\
		lib/scryptenc/scryptenc.c			\
		lib/scryptenc/scryptenc.h			\
		lib/scryptenc/scryptenc_cpuperf.c		\
		lib/scryptenc/scryptenc_cpuperf.h		\
		lib/util/dirutil.c				\
		lib/util/dirutil.h				\
		lib/util/hexlink.c				\
		lib/util/hexlink.h				\
		lib/util/passphrase_entry.c			\
		lib/util/passphrase_entry.h			\
		lib/util/sigquit.c				\
		lib/util/sigquit.h				\
		lib/util/tvmath.c				\
		lib/util/tvmath.h				\
		lib-platform/crypto/crypto_scrypt.c		\
		lib-platform/crypto/crypto_scrypt.h		\
		lib-platform/platform.h				\
		lib-platform/network/tsnetwork_cork.c		\
		lib-platform/network/tsnetwork_cork.h		\
		lib-platform/util/memlimit.c			\
		lib-platform/util/memlimit.h			\
		lib-platform/util/ts_getfstype.c		\
		lib-platform/util/ts_getfstype.h		\
		libcperciva/alg/sha256.c			\
		libcperciva/alg/sha256.h			\
		libcperciva/alg/sha256_arm.h			\
		libcperciva/alg/sha256_shani.h			\
		libcperciva/alg/sha256_sse2.h			\
		libcperciva/cpusupport/cpusupport.h		\
		libcperciva/cpusupport/cpusupport_arm_aes.c	\
		libcperciva/cpusupport/cpusupport_arm_sha256.c	\
		libcperciva/cpusupport/cpusupport_x86_aesni.c	\
		libcperciva/cpusupport/cpusupport_x86_rdrand.c	\
		libcperciva/cpusupport/cpusupport_x86_shani.c	\
		libcperciva/cpusupport/cpusupport_x86_sse2.c	\
		libcperciva/cpusupport/cpusupport_x86_ssse3.c	\
		libcperciva/crypto/crypto_aes.c			\
		libcperciva/crypto/crypto_aes.h			\
		libcperciva/crypto/crypto_aes_aesni.h		\
		libcperciva/crypto/crypto_aes_aesni_m128i.h	\
		libcperciva/crypto/crypto_aes_arm.h		\
		libcperciva/crypto/crypto_aes_arm_u8.h		\
		libcperciva/crypto/crypto_aesctr.c		\
		libcperciva/crypto/crypto_aesctr.h		\
		libcperciva/crypto/crypto_aesctr_aesni.h	\
		libcperciva/crypto/crypto_aesctr_arm.h		\
		libcperciva/crypto/crypto_dh.c			\
		libcperciva/crypto/crypto_dh.h			\
		libcperciva/crypto/crypto_dh_group14.c		\
		libcperciva/crypto/crypto_dh_group14.h		\
		libcperciva/crypto/crypto_entropy.c		\
		libcperciva/crypto/crypto_entropy.h		\
		libcperciva/crypto/crypto_entropy_rdrand.h	\
		libcperciva/crypto/crypto_verify_bytes.c	\
		libcperciva/crypto/crypto_verify_bytes.h	\
		libcperciva/datastruct/elasticarray.c		\
		libcperciva/datastruct/elasticarray.h		\
		libcperciva/datastruct/mpool.h			\
		libcperciva/datastruct/ptrheap.c		\
		libcperciva/datastruct/ptrheap.h		\
		libcperciva/datastruct/timerqueue.c		\
		libcperciva/datastruct/timerqueue.h		\
		libcperciva/events/events.c			\
		libcperciva/events/events.h			\
		libcperciva/events/events_immediate.c		\
		libcperciva/events/events_internal.h		\
		libcperciva/events/events_network.c		\
		libcperciva/events/events_network_selectstats.c	\
		libcperciva/events/events_timer.c		\
		libcperciva/external/queue/queue.h		\
		libcperciva/network/network.h			\
		libcperciva/network/network_accept.c		\
		libcperciva/network/network_connect.c		\
		libcperciva/network/network_read.c		\
		libcperciva/network/network_write.c		\
		libcperciva/util/align_ptr.h			\
		libcperciva/util/asprintf.c			\
		libcperciva/util/asprintf.h			\
		libcperciva/util/b64encode.c			\
		libcperciva/util/b64encode.h			\
		libcperciva/util/ctassert.h			\
		libcperciva/util/entropy.c			\
		libcperciva/util/entropy.h			\
		libcperciva/util/getopt.c			\
		libcperciva/util/getopt.h			\
		libcperciva/util/hexify.c			\
		libcperciva/util/hexify.h			\
		libcperciva/util/humansize.c			\
		libcperciva/util/humansize.h			\
		libcperciva/util/imalloc.h			\
		libcperciva/util/insecure_memzero.c		\
		libcperciva/util/insecure_memzero.h		\
		libcperciva/util/monoclock.c			\
		libcperciva/util/monoclock.h			\
		libcperciva/util/parsenum.h			\
		libcperciva/util/readpass.c			\
		libcperciva/util/readpass.h			\
		libcperciva/util/readpass_file.c		\
		libcperciva/util/sock.c				\
		libcperciva/util/sock.h				\
		libcperciva/util/sock_internal.h		\
		libcperciva/util/sock_util.c			\
		libcperciva/util/sock_util.h			\
		libcperciva/util/sysendian.h			\
		libcperciva/util/ttyfd.c			\
		libcperciva/util/ttyfd.h			\
		libcperciva/util/warnp.c			\
		libcperciva/util/warnp.h

nodist_lib_libtarsnap_a_SOURCES=				\
		cpusupport-config.h

lib_libtarsnap_a_CPPFLAGS=				\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/libarchive		\
		-I$(top_srcdir)/lib			\
		-I$(top_srcdir)/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/scryptenc		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/lib-platform		\
		-I$(top_srcdir)/lib-platform/crypto	\
		-I$(top_srcdir)/lib-platform/network	\
		-I$(top_srcdir)/lib-platform/util	\
		-I$(top_srcdir)/libcperciva/alg		\
		-I$(top_srcdir)/libcperciva/cpusupport	\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/datastruct	\
		-I$(top_srcdir)/libcperciva/events	\
		-I$(top_srcdir)/libcperciva/external/queue \
		-I$(top_srcdir)/libcperciva/network	\
		-I$(top_srcdir)/libcperciva/util	\
		-DCPUSUPPORT_CONFIG_FILE=\"cpusupport-config.h\" \
		-D_POSIX_C_SOURCE=200809L		\
		-D_XOPEN_SOURCE=700			\
		-DTSSERVER=\"$(TSSERVER)\"		\
		${CFLAGS_POSIX}

LIBTARSNAP_A=	lib/libtarsnap.a

cpusupport-config.h:
	( export CC="${CC}"; export CFLAGS="${CFLAGS}"; command -p sh $(srcdir)/libcperciva/cpusupport/Build/cpusupport.sh "$$PATH" ) > cpusupport-config.h.tmp && command -p mv cpusupport-config.h.tmp cpusupport-config.h

# Add libcperciva files to dist
#
# crypto_aesctr_shared.c is in this list because it can't be included in the
# _SOURCES because it should only be included as part of another translation
# unit.
EXTRA_DIST=								\
		libcperciva/POSIX/README				\
		libcperciva/POSIX/posix-abstract-declarator.c		\
		libcperciva/POSIX/posix-cflags.sh			\
		libcperciva/POSIX/posix-clock_gettime.c			\
		libcperciva/POSIX/posix-clock_realtime.c		\
		libcperciva/POSIX/posix-inet-addrstrlen.c		\
		libcperciva/POSIX/posix-inet6-addrstrlen.c		\
		libcperciva/POSIX/posix-l.sh				\
		libcperciva/POSIX/posix-msg_nosignal.c			\
		libcperciva/POSIX/posix-restrict.c			\
		libcperciva/POSIX/posix-stat-st_mtim.c			\
		libcperciva/POSIX/posix-trivial.c			\
		libcperciva/cpusupport/Build/cpusupport.sh		\
		libcperciva/cpusupport/Build/cpusupport-ARM-AES.c	\
		libcperciva/cpusupport/Build/cpusupport-ARM-SHA256.c	\
		libcperciva/cpusupport/Build/cpusupport-HWCAP-ELF_AUX_INFO.c \
		libcperciva/cpusupport/Build/cpusupport-HWCAP-GETAUXVAL.c \
		libcperciva/cpusupport/Build/cpusupport-X86-AESNI.c	\
		libcperciva/cpusupport/Build/cpusupport-X86-CPUID.c	\
		libcperciva/cpusupport/Build/cpusupport-X86-CPUID_COUNT.c \
		libcperciva/cpusupport/Build/cpusupport-X86-RDRAND.c	\
		libcperciva/cpusupport/Build/cpusupport-X86-SHANI.c	\
		libcperciva/cpusupport/Build/cpusupport-X86-SSE2.c	\
		libcperciva/cpusupport/Build/cpusupport-X86-SSSE3.c	\
		libcperciva/crypto/crypto_aesctr_shared.c

BUILT_SOURCES=	cpusupport-config.h
CLEANFILES+=	cpusupport-config.h cpusupport-config.h.tmp

lib_libtarsnap_aesni_a_SOURCES=				\
		libcperciva/crypto/crypto_aes_aesni.c	\
		libcperciva/crypto/crypto_aesctr_aesni.c
nodist_lib_libtarsnap_aesni_a_SOURCES=			\
		cpusupport-config.h
lib_libtarsnap_aesni_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_aesni_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_X86_AESNI}`

LIBTARSNAP_A+=	lib/libtarsnap_aesni.a

lib_libtarsnap_arm_aes_a_SOURCES=				\
		libcperciva/crypto/crypto_aes_arm.c		\
		libcperciva/crypto/crypto_aesctr_arm.c
nodist_lib_libtarsnap_arm_aes_a_SOURCES=			\
		cpusupport-config.h
lib_libtarsnap_arm_aes_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_arm_aes_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_ARM_AES}`

LIBTARSNAP_A+=	lib/libtarsnap_arm_aes.a

lib_libtarsnap_rdrand_a_SOURCES=				\
		libcperciva/crypto/crypto_entropy_rdrand.c
nodist_lib_libtarsnap_rdrand_a_SOURCES=			\
		cpusupport-config.h
lib_libtarsnap_rdrand_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_rdrand_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_X86_RDRAND}`

LIBTARSNAP_A+=	lib/libtarsnap_rdrand.a

lib_libtarsnap_sse2_a_SOURCES=				\
		libcperciva/alg/sha256_sse2.c		\
		lib/crypto/crypto_scrypt_smix_sse2.c
nodist_lib_libtarsnap_sse2_a_SOURCES=			\
		cpusupport-config.h
lib_libtarsnap_sse2_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_sse2_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_X86_SSE2}`

LIBTARSNAP_A+=	lib/libtarsnap_sse2.a

lib_libtarsnap_shani_a_SOURCES=				\
		libcperciva/alg/sha256_shani.c
nodist_lib_libtarsnap_shani_a_SOURCES=			\
		cpusupport-config.h
lib_libtarsnap_shani_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_shani_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_X86_SHANI} $${CFLAGS_X86_SSSE3}`

LIBTARSNAP_A+=	lib/libtarsnap_shani.a

lib_libtarsnap_arm_sha256_a_SOURCES=				\
		libcperciva/alg/sha256_arm.c
nodist_lib_libtarsnap_arm_sha256_a_SOURCES=			\
		cpusupport-config.h
lib_libtarsnap_arm_sha256_a_CPPFLAGS=$(lib_libtarsnap_a_CPPFLAGS)
lib_libtarsnap_arm_sha256_a_CFLAGS=`. ./cpusupport-config.h; echo $${CFLAGS_ARM_SHA256}`

LIBTARSNAP_A+=	lib/libtarsnap_arm_sha256.a


#
#
# tarsnap source, docs, etc.
#
#

tarsnap_SOURCES=					\
		tar/bsdtar.c				\
		tar/bsdtar.h				\
		tar/bsdtar_platform.h			\
		tar/ccache/ccache.h			\
		tar/ccache/ccache_entry.c		\
		tar/ccache/ccache_internal.h		\
		tar/ccache/ccache_read.c		\
		tar/ccache/ccache_write.c		\
		tar/chunks/chunks.h			\
		tar/chunks/chunks_delete.c		\
		tar/chunks/chunks_directory.c		\
		tar/chunks/chunks_internal.h		\
		tar/chunks/chunks_read.c		\
		tar/chunks/chunks_stats.c		\
		tar/chunks/chunks_stats_internal.c	\
		tar/chunks/chunks_transaction.c		\
		tar/chunks/chunks_write.c		\
		tar/cmdline.c				\
		tar/getdate.c				\
		tar/glue/archive_multitape.c		\
		tar/glue/archive_multitape.h		\
		tar/glue/tape.c				\
		tar/matching.c				\
		tar/multitape/chunkify.c		\
		tar/multitape/chunkify.h		\
		tar/multitape/multitape.h		\
		tar/multitape/multitape_chunkiter.c	\
		tar/multitape/multitape_delete.c	\
		tar/multitape/multitape_fsck.c		\
		tar/multitape/multitape_internal.h	\
		tar/multitape/multitape_metadata.c	\
		tar/multitape/multitape_metaindex.c	\
		tar/multitape/multitape_nuke.c		\
		tar/multitape/multitape_read.c		\
		tar/multitape/multitape_recover.c	\
		tar/multitape/multitape_stats.c		\
		tar/multitape/multitape_transaction.c	\
		tar/multitape/multitape_write.c		\
		tar/read.c				\
		tar/siginfo.c				\
		tar/storage/storage.h			\
		tar/storage/storage_delete.c		\
		tar/storage/storage_directory.c		\
		tar/storage/storage_internal.h		\
		tar/storage/storage_read.c		\
		tar/storage/storage_read_cache.c	\
		tar/storage/storage_read_cache.h	\
		tar/storage/storage_transaction.c	\
		tar/storage/storage_write.c		\
		tar/subst.c				\
		tar/tarsnap_opt.h			\
		tar/tree.c				\
		tar/tree.h				\
		tar/util.c				\
		tar/write.c

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
		libarchive/archive_entry.h		\
		libarchive/archive.h			\
		$(LIBTARSNAP_A)

tarsnap_LDADD= libarchive/libarchive.a $(LIBTARSNAP_A)
tarsnap_CPPFLAGS=					\
		-I$(top_srcdir)/libarchive		\
		-DLIBARCHIVE_STATIC			\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/tar/ccache		\
		-I$(top_srcdir)/tar/chunks		\
		-I$(top_srcdir)/tar/glue		\
		-I$(top_srcdir)/tar/multitape		\
		-I$(top_srcdir)/tar/storage		\
		-I$(top_srcdir)/lib			\
		-I$(top_srcdir)/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/lib-platform		\
		-I$(top_srcdir)/lib-platform/util	\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/datastruct	\
		-I$(top_srcdir)/libcperciva/util	\
		"-DETC_TARSNAP_CONF=\"${sysconfdir}/tarsnap.conf\""	\
		-DUSERAGENT=\"tarsnap-${VERSION}\"
tarsnap_man_MANS=	tar/tarsnap.1 tar/tarsnap.conf.5

#
# tarsnap-keygen source, docs, etc.
#
#

tarsnap_keygen_SOURCES=	keygen/keygen.c			\
		keygen/keygen.h				\
		keygen/keygen_actual.c			\
		keygen/keygen_network.c

tarsnap_keygen_LDADD= $(LIBTARSNAP_A)
tarsnap_keygen_CPPFLAGS=				\
		-I$(top_srcdir)/libarchive		\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/keygen			\
		-I$(top_srcdir)/lib			\
		-I$(top_srcdir)/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/lib-platform		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util	\
		-DUSERAGENT=\"tarsnap-keygen-${VERSION}\"

tarsnap_keygen_man_MANS=	keygen/tarsnap-keygen.1

#
# tarsnap-keyregen source, docs, etc.
#
#

tarsnap_keyregen_SOURCES=	keyregen/keyregen.c	\
		keygen/keygen.h				\
		keygen/keygen_actual.c			\
		keygen/keygen_network.c

tarsnap_keyregen_LDADD= $(LIBTARSNAP_A)
tarsnap_keyregen_CPPFLAGS=				\
		-I$(top_srcdir)/libarchive		\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/keygen			\
		-I$(top_srcdir)/lib			\
		-I$(top_srcdir)/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/lib-platform		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util	\
		-DUSERAGENT=\"tarsnap-keyregen-${VERSION}\"

tarsnap_keyregen_man_MANS=	keyregen/tarsnap-keyregen.1

#
#
# tarsnap-recrypt source, docs, etc.
#
#

tarsnap_recrypt_SOURCES=	recrypt/recrypt.c	\
		tar/chunks/chunks_delete.c		\
		tar/chunks/chunks_directory.c		\
		tar/chunks/chunks_stats.c		\
		tar/chunks/chunks_stats_internal.c	\
		tar/chunks/chunks_transaction.c		\
		tar/chunks/chunks_write.c		\
		tar/multitape/multitape_metadata.c	\
		tar/multitape/multitape_transaction.c	\
		tar/storage/storage_delete.c		\
		tar/storage/storage_directory.c		\
		tar/storage/storage_read.c		\
		tar/storage/storage_read_cache.c	\
		tar/storage/storage_read_cache.h	\
		tar/storage/storage_transaction.c	\
		tar/storage/storage_write.c

tarsnap_recrypt_LDADD= $(LIBTARSNAP_A)
tarsnap_recrypt_CPPFLAGS=				\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/libarchive		\
		-I$(top_srcdir)/tar/chunks		\
		-I$(top_srcdir)/tar/multitape		\
		-I$(top_srcdir)/tar/storage		\
		-I$(top_srcdir)/lib			\
		-I$(top_srcdir)/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/netpacket		\
		-I$(top_srcdir)/lib/netproto		\
		-I$(top_srcdir)/lib/network		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/lib-platform		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util	\
		-DUSERAGENT=\"tarsnap-recrypt-${VERSION}\"
tarsnap_recrypt_man_MANS=	recrypt/tarsnap-recrypt.1

#
#
# tarsnap-keymgmt source, docs, etc.
#
#

tarsnap_keymgmt_SOURCES=	keymgmt/keymgmt.c

tarsnap_keymgmt_LDADD= $(LIBTARSNAP_A)
tarsnap_keymgmt_CPPFLAGS=				\
		-I$(top_srcdir)/tar			\
		-I$(top_srcdir)/libarchive		\
		-I$(top_srcdir)/lib			\
		-I$(top_srcdir)/lib/crypto		\
		-I$(top_srcdir)/lib/datastruct		\
		-I$(top_srcdir)/lib/keyfile		\
		-I$(top_srcdir)/lib/util		\
		-I$(top_srcdir)/lib-platform		\
		-I$(top_srcdir)/libcperciva/crypto	\
		-I$(top_srcdir)/libcperciva/util

tarsnap_keymgmt_man_MANS=	keymgmt/tarsnap-keymgmt.1

#
# Test framework and support programs
#
#

.PHONY: test
test: all
	$(top_srcdir)/tests/test_tarsnap.sh .

# Detect potential memory leaks from libraries.
tests_valgrind_potential_memleaks_SOURCES = tests/valgrind/potential-memleaks.c

tests_valgrind_potential_memleaks_CPPFLAGS =		\
		-D_POSIX_C_SOURCE=200809L		\
		-D_XOPEN_SOURCE=700			\
		${CFLAGS_POSIX}

# Add test files to dist
EXTRA_DIST+=	tests/01-trivial.sh			\
		tests/01-trivial.sh			\
		tests/02-c-dry-run-no-keyfile.sh	\
		tests/03-c-dry-run-fake-keyfile.sh	\
		tests/04-c-d-real-keyfile.sh		\
		tests/05-passphrase.sh			\
		tests/06-progress-output.sh		\
		tests/06-progress-output-pb.good	\
		tests/06-progress-output-v.good		\
		tests/06-progress-output-vpb-1k.good	\
		tests/06-progress-output-vpb-20k.good	\
		tests/fake-passphrased.keys		\
		tests/fake.keys				\
		tests/shared_test_functions.sh		\
		tests/shared_valgrind_functions.sh	\
		tests/test_tarsnap.sh

#  Tarsnap configuration file
if INSTALL_CONF_NO_SAMPLE
tar/tarsnap.conf: tar/tarsnap.conf.sample
	cp $< $@

sysconf_DATA	=	tar/tarsnap.conf
CLEANFILES	+=	tar/tarsnap.conf
else
sysconf_DATA	=	tar/tarsnap.conf.sample
endif

# Use the right version of the man pages depending on whether we have mdoc
# macros available on this system.
tar/tarsnap.1-man tar/tarsnap.1-mdoc \
tar/tarsnap.conf.5-man tar/tarsnap.conf.5-mdoc: tar/tarsnap.1-man.in \
		tar/tarsnap.1-mdoc.in tar/tarsnap.conf.5-man.in \
		tar/tarsnap.conf.5-mdoc.in
	sed -e 's|@sysconfdir[@]|${sysconfdir}|g' ${srcdir}/$@.in > $@.tmp
	mv $@.tmp $@
tar/tarsnap.1:	tar/tarsnap.1-@MANVER@
	cat tar/tarsnap.1-@MANVER@ > tar/tarsnap.1.tmp
	mv $@.tmp $@
tar/tarsnap.conf.5: tar/tarsnap.conf.5-@MANVER@
	cat tar/tarsnap.conf.5-@MANVER@ > tar/tarsnap.conf.5.tmp
	mv $@.tmp $@
keygen/tarsnap-keygen.1: keygen/tarsnap-keygen.1-@MANVER@
	cat $(top_srcdir)/keygen/tarsnap-keygen.1-@MANVER@ > keygen/tarsnap-keygen.1.tmp
	mv $@.tmp $@
keyregen/tarsnap-keyregen.1: keyregen/tarsnap-keyregen.1-@MANVER@
	cat $(top_srcdir)/keyregen/tarsnap-keyregen.1-@MANVER@ > keyregen/tarsnap-keyregen.1.tmp
	mv $@.tmp $@
recrypt/tarsnap-recrypt.1: recrypt/tarsnap-recrypt.1-@MANVER@
	cat $(top_srcdir)/recrypt/tarsnap-recrypt.1-@MANVER@ > recrypt/tarsnap-recrypt.1.tmp
	mv $@.tmp $@
keymgmt/tarsnap-keymgmt.1: keymgmt/tarsnap-keymgmt.1-@MANVER@
	cat $(top_srcdir)/keymgmt/tarsnap-keymgmt.1-@MANVER@ > keymgmt/tarsnap-keymgmt.1.tmp
	mv $@.tmp $@

# Add man files to dist
EXTRA_DIST+=	keygen/tarsnap-keygen.1-man		\
		keygen/tarsnap-keygen.1-mdoc		\
		keymgmt/tarsnap-keymgmt.1-man		\
		keymgmt/tarsnap-keymgmt.1-mdoc		\
		keyregen/tarsnap-keyregen.1-man		\
		keyregen/tarsnap-keyregen.1-mdoc	\
		recrypt/tarsnap-recrypt.1-man		\
		recrypt/tarsnap-recrypt.1-mdoc		\
		tar/tarsnap.1-man.in			\
		tar/tarsnap.1-mdoc.in			\
		tar/tarsnap.conf.5-man.in		\
		tar/tarsnap.conf.5-mdoc.in		\
		tar/tarsnap.conf.sample
CLEANFILES+=   tar/tarsnap.1-mdoc tar/tarsnap.conf.5-mdoc

# Add misc files to dist
EXTRA_DIST+=	BUILDING				\
		lib/netpacket/packets.txt		\
		libcperciva/COPYRIGHT			\
		misc/describe-options.txt		\
		pkg/

#
# Misc helper scripts
#
#
if INSTALL_BASH_COMPLETION
bashcompdir = $(BASH_COMPLETION_DIR)
dist_bashcomp_DATA = misc/bash_completion.d/tarsnap	\
	misc/bash_completion.d/tarsnap-keygen		\
	misc/bash_completion.d/tarsnap-keymgmt		\
	misc/bash_completion.d/tarsnap-keyregen		\
	misc/bash_completion.d/tarsnap-recrypt
endif
if INSTALL_ZSH_COMPLETION
zshcompdir = $(ZSH_COMPLETION_DIR)
dist_zshcomp_DATA = misc/zsh_completion/_tarsnap
endif
