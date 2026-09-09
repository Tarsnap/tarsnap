#!/bin/sh
# Standalone GNU/POSIX regression. Configure first; no account is required.
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${1:-"$root"}
if [ ! -f "$build/config.h" ]; then
    echo "Configure the project first, or pass a configured build directory." >&2
    exit 2
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
set -- -I"$build"
set -- "$@" -I"$root/lib"
set -- "$@" -I"$root/lib-platform"
set -- "$@" -I"$root/lib-platform/util"
set -- "$@" -I"$root/lib/crypto"
set -- "$@" -I"$root/lib/datastruct"
set -- "$@" -I"$root/lib/keyfile"
set -- "$@" -I"$root/lib/netpacket"
set -- "$@" -I"$root/lib/netproto"
set -- "$@" -I"$root/lib/network"
set -- "$@" -I"$root/lib/util"
set -- "$@" -I"$root/libarchive"
set -- "$@" -I"$root/libcperciva/crypto"
set -- "$@" -I"$root/libcperciva/datastruct"
set -- "$@" -I"$root/libcperciva/util"
set -- "$@" -I"$root/tar"
set -- "$@" -I"$root/tar/ccache"
set -- "$@" -I"$root/tar/chunks"
set -- "$@" -I"$root/tar/multitape"
set -- "$@" -I"$root/tar/storage"
${CC:-cc} -DHAVE_CONFIG_H -DUSERAGENT='"unit-regression"' \
    -std=c99 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    "$@" "$root/tests/unit/storage-commit.c" \
    -Wl,--gc-sections -o "$tmp/storage-commit"
"$tmp/storage-commit"
