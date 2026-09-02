#!/bin/bash
# Host test runner. Builds the real src/*.c against the stub agon headers in
# test/stubs and runs the resulting binaries natively.
#
# These are host tests, not target tests: they cover logic that is independent
# of the eZ80 (buffer arithmetic, cursor bookkeeping). `char` is 1 byte and
# signed on both targets -- -fsigned-char makes that explicit rather than
# relying on the host default -- so truncation behaviour matches the device.
# Anything that depends on the eZ80's 3-byte int still needs a real Agon.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

CFLAGS=(-std=c11 -Wall -Wextra -fsigned-char -g -Isrc -Itest/stubs)

status=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" src/screen.c src/conv.c
    "$OUT/$name" || status=$?
done

exit $status
