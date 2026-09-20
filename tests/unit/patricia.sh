#!/bin/sh
# Standalone UBSan regression; requires a GCC- or Clang-compatible compiler.
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror \
    -fsanitize=undefined -fno-sanitize-recover=undefined \
    -I"$root/lib/datastruct" "$root/tests/unit/patricia.c" \
    "$root/lib/datastruct/patricia.c" -o "$tmp/patricia"
"$tmp/patricia"
