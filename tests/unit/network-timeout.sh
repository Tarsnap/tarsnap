#!/bin/sh
# POSIX socketpair regression: no remote addresses or credentials.
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror \
    -I"$root/tar" -I"$root/lib/network" -I"$root/lib/util" \
    -I"$root/libcperciva/util" "$root/tests/unit/network-timeout.c" \
    "$root/lib/network/tsnetwork_buf.c" -o "$tmp/timeout"
"$tmp/timeout"
