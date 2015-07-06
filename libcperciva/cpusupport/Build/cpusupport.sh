# Should be sourced by `command -p sh mk-h.sh` from within a Makefile
# Standard output should be written to cpusupport-config.h, which is both a
# C header file defining CPUSUPPORT_ARCH_FEATURE macros and sourceable sh
# code which sets CFLAGS_ARCH_FEATURE environment variables.
feature() {
	ARCH=$1
	FEATURE=$2
	shift 2;
	printf "Checking if compiler supports $ARCH $FEATURE feature..." 1>&2
	for CFLAG in "$@"; do
		if ${CC} ${CFLAGS} -D_POSIX_C_SOURCE=200809L ${CFLAG}	\
		    cpusupport-$ARCH-$FEATURE.c 2>/dev/null; then
			rm -f a.out
			break;
		fi
		CFLAG=NOTSUPPORTED;
	done
	case $CFLAG in
	NOTSUPPORTED)
		echo " no" 1>&2
		;;
	"")
		echo " yes" 1>&2
		echo "#define CPUSUPPORT_${ARCH}_${FEATURE}"
		;;
	*)
		echo " yes, via $CFLAG" 1>&2
		echo "#define CPUSUPPORT_${ARCH}_${FEATURE}"
		echo "#ifdef cpusupport_dummy"
		echo "export CFLAGS_${ARCH}_${FEATURE}=\"${CFLAG}\""
		echo "#endif"
		;;
	esac
}

feature X86 CPUID ""
feature X86 AESNI "" "-maes" "-maes -Wno-cast-align" "-maes -Wno-missing-prototypes -Wno-cast-qual"
