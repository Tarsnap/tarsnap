#!/bin/sh
set -eu

srcroot=${1:-.}
buildroot=${2:-.}
srcroot=$(cd "$srcroot" && pwd)
buildroot=$(cd "$buildroot" && pwd)
python=${PYTHON:-python3}
cc=${CC:-cc}
tmp=${TMPDIR:-/tmp}/tarsnap-ccache-read.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$python" "$srcroot/tests/unit/ccache-read.py" \
    --root "$srcroot" --build-root "$buildroot" --cc "$cc" \
    --mode read --output "$tmp/read"

if grep -Eq '^#define[[:space:]]+HAVE_MMAP[[:space:]]+1' "$buildroot/config.h"; then
    "$python" "$srcroot/tests/unit/ccache-read.py" \
        --root "$srcroot" --build-root "$buildroot" --cc "$cc" \
        --mode mmap --output "$tmp/mmap"
fi
