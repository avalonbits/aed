#!/bin/bash
# Host test runner. Builds the real src/*.c against the stub agon headers in
# test/stubs and runs the resulting binaries natively.
#
# These are host tests, not target tests: they cover logic that is independent
# of the eZ80 (buffer arithmetic, cursor bookkeeping, the save path). `char` is
# 1 byte and signed on both targets -- -fsigned-char makes that explicit rather
# than relying on the host default -- so truncation behaviour matches the device.
# Anything that depends on the eZ80's 3-byte int, or on real VDP/MOS behaviour,
# still needs a real Agon or the emulator.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# ASan catches out-of-bounds writes that would otherwise corrupt the heap
# silently -- the buffer-full tests depend on it to prove the bound holds.
CFLAGS=(-std=c11 -Wall -Wextra -fsigned-char -g -fsanitize=address,undefined
        -Isrc -Itest/stubs)

# Model, View and Controller, plus the stubbed platform layer. cmd_ops.c is
# linked so tests can drive whole commands: the two worst bugs so far lived in
# the controller/view interaction, which nothing below that level can reach.
# editor.c stays out -- it is only the blocking input loop.
SRCS=(src/char_buffer.c src/line_buffer.c src/text_buffer.c src/screen.c
      src/conv.c src/cmd_ops.c src/user_input.c src/config.c
      test/stubs/agon_stubs.c)

status=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${SRCS[@]}"
    "$OUT/$name" || status=$?
done

exit $status
