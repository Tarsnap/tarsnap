# Should be sourced by `command -p sh path/to/cpusupport.sh "$PATH"` from
# within a Makefile.
if ! [ ${PATH} = "$1" ]; then
	echo "WARNING: POSIX violation: $SHELL's command -p resets \$PATH" 1>&2
	PATH=$1
fi
# Standard output should be written to cpusupport-config.h, which is both a
# C header file defining CPUSUPPORT_ARCH_FEATURE macros and sourceable sh
# code which sets CFLAGS_ARCH_FEATURE environment variables.
SRCDIR=$(command -p dirname "$0")

feature() {
	ARCH=$1
	FEATURE=$2
	shift 2;
	if ! [ -f ${SRCDIR}/cpusupport-$ARCH-$FEATURE.c ]; then
		return
	fi
	printf "Checking if compiler supports $ARCH $FEATURE feature..." 1>&2
	for CPU_CFLAGS in "$@"; do
		if ${CC} ${CFLAGS} -D_POSIX_C_SOURCE=200809L ${CPU_CFLAGS} \
		    ${SRCDIR}/cpusupport-$ARCH-$FEATURE.c 2>/dev/null; then
			rm -f a.out
			break;
		fi
		CPU_CFLAGS=NOTSUPPORTED;
	done
	case $CPU_CFLAGS in
	NOTSUPPORTED)
		echo " no" 1>&2
		;;
	"")
		echo " yes" 1>&2
		echo "#define CPUSUPPORT_${ARCH}_${FEATURE} 1"
		;;
	*)
		echo " yes, via $CPU_CFLAGS" 1>&2
		echo "#define CPUSUPPORT_${ARCH}_${FEATURE} 1"
		echo "#ifdef cpusupport_dummy"
		echo "export CFLAGS_${ARCH}_${FEATURE}=\"${CPU_CFLAGS}\""
		echo "#endif"
		;;
	esac
}

if [ "$2" = "--all" ]; then
	feature() {
		ARCH=$1
		FEATURE=$2
		echo "#define CPUSUPPORT_${ARCH}_${FEATURE} 1"
	}
fi

# Detect CPU-detection features
feature HWCAP GETAUXVAL ""
feature X86 CPUID ""
feature X86 CPUID_COUNT ""

# Detect specific features
feature X86 AESNI "" "-maes"						\
    "-maes -Wno-cast-align"						\
    "-maes -Wno-missing-prototypes -Wno-cast-qual"			\
    "-maes -Wno-missing-prototypes -Wno-cast-qual -Wno-cast-align"	\
    "-maes -Wno-missing-prototypes -Wno-cast-qual -Wno-cast-align	\
    -DBROKEN_MM_LOADU_SI64"
feature X86 RDRAND "" "-mrdrnd"
feature X86 SHANI "" "-msse2 -msha"					\
    "-msse2 -msha -Wno-cast-align"
feature X86 SSE2 ""							\
    "-Wno-cast-align"							\
    "-msse2"								\
    "-msse2 -Wno-cast-align"
feature X86 SSE42 "" "-msse4.2"						\
    "-msse4.2 -Wno-cast-align"						\
    "-msse4.2 -Wno-cast-align -fno-strict-aliasing"			\
    "-msse4.2 -Wno-cast-align -fno-strict-aliasing -Wno-cast-qual"
feature X86 SSE42_64 "" "-msse4.2"					\
    "-msse4.2 -Wno-cast-align"						\
    "-msse4.2 -Wno-cast-align -fno-strict-aliasing"			\
    "-msse4.2 -Wno-cast-align -fno-strict-aliasing -Wno-cast-qual"
feature X86 SSSE3 "" "-mssse3"						\
    "-mssse3 -Wno-cast-align"

# Detect specific ARM features
feature ARM AES "-march=armv8.1-a+crypto"				\
    "-march=armv8.1-a+crypto -D__ARM_ACLE=200"
feature ARM CRC32_64 "-march=armv8.1-a"					\
    "-march=armv8.1-a+crc"						\
    "-march=armv8.1-a+crc -Wno-cast-align"				\
    "-march=armv8.1-a -D__ARM_ACLE=200"
feature ARM SHA256 "-march=armv8.1-a+crypto"				\
    "-march=armv8.1-a+crypto -Wno-cast-align"				\
    "-march=armv8.1-a+crypto -D__ARM_ACLE=200"
