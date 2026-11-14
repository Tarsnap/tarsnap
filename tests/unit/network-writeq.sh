#!/bin/sh
# Standalone queue regression; uses no sockets or hosted service.
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror \
    -I"$root/lib/network" -I"$root/lib-platform/network" \
    -I"$root/lib/util" "$root/tests/unit/network-writeq.c" \
    "$root/lib/network/tsnetwork_writeq.c" -o "$tmp/writeq"
"$tmp/writeq"
