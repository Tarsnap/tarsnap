# Should be sourced by `command -p sh path/to/apisupport.sh "$PATH"` from
# within a Makefile.
if ! [ "${PATH}" = "$1" ]; then
	echo "WARNING: POSIX violation: ${SHELL}'s command -p resets \$PATH" 1>&2
	PATH=$1
fi

# Standard output should be written to apisupport-config.h, which is both a
# C header file defining APISUPPORT_PLATFORM_FEATURE macros and sourceable sh
# code which sets CFLAGS_PLATFORM_FEATURE environment variables.
SRCDIR=$(command -p dirname "$0")

CFLAGS_HARDCODED="-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700"

# Do we want to record stderr to a file?
if [ "${DEBUG:-0}" -eq "0" ]; then
	outcc="/dev/null"
else
	outcc="apisupport-stderr.log"
	rm -f "${outcc}"
fi

feature() {
	PLATFORM=$1
	FEATURE=$2
	EXTRALIB=$3
	shift 3;

	# Bail if we didn't include this feature in this source tree.
	feature_filename="${SRCDIR}/apisupport-${PLATFORM}-${FEATURE}.c"
	if ! [ -f "${feature_filename}" ]; then
		return
	fi

	# Check if we can compile this feature (and any required arguments).
	printf "Checking if compiler supports %s %s feature..."		\
	    "${PLATFORM}" "${FEATURE}" 1>&2
	for API_CFLAGS in "$@"; do
		if ${CC} ${CPPFLAGS} ${CFLAGS} ${CFLAGS_HARDCODED}	\
		    ${API_CFLAGS} "${feature_filename}" ${LDADD_EXTRA}	\
		    ${EXTRALIB}	2>>"${outcc}"; then
			rm -f a.out
			break;
		fi
		API_CFLAGS=NOTSUPPORTED;
	done
	case ${API_CFLAGS} in
	NOTSUPPORTED)
		echo " no" 1>&2
		;;
	"")
		echo " yes" 1>&2
		echo "#define APISUPPORT_${PLATFORM}_${FEATURE} 1"
		;;
	*)
		echo " yes, via ${API_CFLAGS}" 1>&2
		echo "#define APISUPPORT_${PLATFORM}_${FEATURE} 1"
		echo "#ifdef apisupport_dummy"
		echo "export CFLAGS_${PLATFORM}_${FEATURE}=\"${API_CFLAGS}\""
		echo "#endif"
		;;
	esac
}

if [ "$2" = "--all" ]; then
	feature() {
		PLATFORM=$1
		FEATURE=$2
		echo "#define APISUPPORT_${PLATFORM}_${FEATURE} 1"
	}
fi

# Detect how to compile non-POSIX code.
feature NONPOSIX SETGROUPS "" ""			\
	"-U_POSIX_C_SOURCE -U_XOPEN_SOURCE"		\
	"-U_POSIX_C_SOURCE -U_XOPEN_SOURCE -Wno-reserved-id-macro"

# Detect how to compile libssl and libcrypto code.
feature LIBSSL HOST_NAME "-lssl" ""			\
	"-Wno-cast-qual"
feature LIBCRYPTO LOW_LEVEL_AES "-lcrypto" ""		\
	"-Wno-deprecated-declarations"
