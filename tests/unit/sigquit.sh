#!/bin/sh
# Standalone C11 type-contract and POSIX signal test; no TTY or server needed.
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror \
    -I"$root/lib/util" -I"$root/libcperciva/util" \
    "$root/tests/unit/sigquit.c" "$root/lib/util/sigquit.c" \
    -o "$tmp/sigquit"
"$tmp/sigquit"
