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

# An optional filter: `./test/run.sh paging` runs test_paging and nothing else,
# and skips the three checks above with it. A whole run is the default and is
# what anything automated should use; this is for working on one thing.
FILTER=${1:-}

status=0

if [ -z "$FILTER" ]; then
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
fi

# The sources are compiled once and linked into each test, rather than compiled
# again for every one of them. Twenty-five tests against fifteen sources was
# three hundred and seventy-five compiles a run, and all but fifteen of them
# were the same work over again -- forty seconds, which is long enough that it
# changes how often a run happens.
#
# Each object is still built from source on every run, so the staleness this
# file warns about above is still impossible: the objects live in a temporary
# directory that goes with the run.
OBJS=()
for s in "${SRCS[@]}"; do
    o="$OUT/$(basename "$s" .c).o"
    if ! cc "${CFLAGS[@]}" -c -o "$o" "$s"; then
        echo "FAIL  $s did not compile"

        exit 1
    fi
    OBJS+=("$o")
done

for t in test/test_*.c; do
    name=$(basename "$t" .c)
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        continue
    fi
    echo "=== $name ==="
    if ! cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${OBJS[@]}"; then
        echo "FAIL  $name did not compile"
        status=1
        continue
    fi
    "$OUT/$name" || status=$?
done

exit $status
