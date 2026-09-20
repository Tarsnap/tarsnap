#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${1:-"$root"}
cc=${CC:-cc}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

if [ ! -f "$build/config.h" ]; then
	echo "Configure the project before running this regression." >&2
	exit 2
fi

case $(uname -s) in
Darwin)
	gc_sections=-Wl,-dead_strip
	;;
*)
	gc_sections=-Wl,--gc-sections
	;;
esac

"$cc" -DHAVE_CONFIG_H -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
    -std=c99 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections \
    -I"$build" -I"$root/lib" -I"$root/lib-platform" \
    -I"$root/lib-platform/util" -I"$root/lib/crypto" \
    -I"$root/lib/datastruct" -I"$root/lib/netpacket" \
    -I"$root/lib/netproto" -I"$root/lib/network" -I"$root/lib/util" \
    -I"$root/libarchive" -I"$root/libcperciva/crypto" \
    -I"$root/libcperciva/datastruct" -I"$root/libcperciva/util" \
    -I"$root/tar" -I"$root/tar/chunks" -I"$root/tar/storage" \
    "$root/tests/unit/padme-shift.c" "$gc_sections" \
    -o "$tmp/padme-shift"

"$tmp/padme-shift"
