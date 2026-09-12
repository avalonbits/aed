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
# editor.c is linked too: ed_init applies the settings file, and that policy is
# worth testing. So is read_input, which is where a chord MOS reports correctly
# can still be lost -- keys.c is linked for it, over a stubbed event queue.
SRCS=(src/bootfont.c src/char_buffer.c src/doc_store.c src/line_buffer.c src/text_buffer.c src/screen.c
      src/conv.c src/cmd_ops.c src/user_input.c src/config.c src/clipboard.c src/editor.c
      src/keys.c src/undo.c test/stubs/agon_stubs.c)

status=0

# The build itself, which nothing below can check: these tests compile every
# source together on every run, so a stale object file is invisible to them.
# Skipped when the AgonDev toolchain is not on PATH.
./test/build_deps.sh || status=$?

# The shipped fonts: AED reads their height from the file size, so a mangled
# binary asset is a font of the wrong height and nothing else here would notice.
./test/fonts.sh || status=$?

# Stack frames, which the host cannot see at all: `(ix + d)` is a signed byte on
# the eZ80, so a frame past 128 bytes pays an address computation on every local
# access. The usual way one arrives is a 256-byte buffer, and nothing in the C
# says it happened.
./test/frames.sh || status=$?

for t in test/test_*.c; do
    name=$(basename "$t" .c)
    echo "=== $name ==="
    if ! cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${SRCS[@]}"; then
        echo "FAIL  $name did not compile"
        status=1
        continue
    fi
    "$OUT/$name" || status=$?
done

exit $status
