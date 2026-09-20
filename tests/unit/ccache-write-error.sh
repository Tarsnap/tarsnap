#!/bin/sh
# Exercise ccache write/remove failure unwinding using the real implementation.
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

case $(uname -s) in
Darwin) gc_sections=-Wl,-dead_strip ;;
*) gc_sections=-Wl,--gc-sections ;;
esac

${CC:-cc} -DHAVE_CONFIG_H -DUSERAGENT='"unit-regression"' -D_GNU_SOURCE \
    -std=c99 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    "$@" "$root/tests/unit/ccache-write-error.c" \
    "$root/lib/datastruct/patricia.c" "$gc_sections" \
    -o "$tmp/ccache-write-error"

cases=0
run_case() {
    "$tmp/ccache-write-error" "$@" >/dev/null
    cases=$((cases + 1))
}

run_case empty 0 0
run_case skipped 6 0
run_case mixed 6 0

for count in 1 3 6; do
    for name in success fopen count record-before data-before fsync fclose unlink rename dirsync; do
        run_case "$name" "$count" 0
    done
    run_case asprintf "$count" 1
    run_case asprintf "$count" 2

    for name in record-after data-after; do
        i=1
        while [ "$i" -le "$count" ]; do
            run_case "$name" "$count" "$i"
            i=$((i + 1))
        done
    done

    i=1
    max=$((4 * count + 1))
    while [ "$i" -le "$max" ]; do
        run_case fwrite "$count" "$i"
        i=$((i + 1))
    done

    case "$count" in
    1) max=1 ;;
    3) max=2 ;;
    6) max=4 ;;
    esac
    i=1
    while [ "$i" -le "$max" ]; do
        run_case malloc "$count" "$i"
        i=$((i + 1))
    done
done

for name in remove-existing remove-missing remove-asprintf remove-unlink; do
    run_case "$name" 0 0
done

[ "$cases" -eq 113 ]
printf 'PASS: %d ccache write/remove failure-path scenarios\n' "$cases"
