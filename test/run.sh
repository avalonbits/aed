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
WARN=(-std=c11 -Wall -Wextra -fsigned-char -g -fsanitize=address,undefined)

# Each layer sees itself, the layers under it and the platform stubs, and no
# more: the core only itself, the UI the core too, AED all three. Compiling
# them this way is half of what proves the split -- a core source that reached
# for a UI header, or a UI source for one of AED's, would not compile. The
# other half is the links below.
CORE_CFLAGS=("${WARN[@]}" -Isrc/core -Itest/stubs)
UI_CFLAGS=("${WARN[@]}" -Isrc/core -Isrc/ui -Itest/stubs)
CFLAGS=("${WARN[@]}" -Isrc/core -Isrc/ui -Isrc -Itest/stubs)

# The three layers, plus the stubbed platform layer. The UI is linked so tests
# can drive whole commands: the two worst bugs so far lived in the
# controller/view interaction, which nothing below that level can reach. AED's
# own files are linked too: ed_init applies the settings file, and that policy
# is worth testing. So is ed_translate, which is where a chord MOS reports
# correctly can still be lost -- keys.c is linked for it, over a stubbed event
# queue. main.c is the one source left out: every test has its own main.
CORE_SRCS=(src/core/*.c)
UI_SRCS=(src/ui/*.c)
AED_SRCS=()
for s in src/*.c; do
    [ "$s" = src/main.c ] || AED_SRCS+=("$s")
done
STUB_SRCS=(test/stubs/agon_stubs.c)

# An optional filter: `./test/run.sh paging` runs test_paging and nothing else,
# and skips the three checks above with it. A whole run is the default and is
# what anything automated should use; this is for working on one thing.
FILTER=${1:-}

status=0

if [ -z "$FILTER" ]; then
# One name per file across the three layers. Each is on everyone's include
# path, so two headers of the same name would be found in whichever directory
# comes first; and frames.sh and the bench key their output by file name, so
# two sources of the same name would overwrite one another there.
dups=$(for f in src/*.[ch] src/core/*.[ch] src/ui/*.[ch]; do basename "$f"; done \
       | sort | uniq -d | tr '\n' ' ')
if [ -z "$dups" ]; then
    printf 'PASS  %-52s %s\n' "every source and header has a name of its own" "yes"
else
    printf 'FAIL  %-52s %s\n' "the same name in more than one layer" "$dups"
    status=1
fi

# The libraries: that AED is linked from them, that `make lib` builds them, and
# that the package mklibs.sh makes is enough to build another program with.
./test/libs.sh || status=$?

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

# The documents, which nothing else reads: a heading renamed out from under the
# index, a file moved, or a function that drifted away from a `#L123` deep link.
# The last of those rots without anyone touching the document.
./test/docs.sh || status=$?
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
compile() {    # compile <flags array name> <objects array name> <sources...>
    local -n flags=$1 objs=$2
    shift 2
    for s in "$@"; do
        # Named by path, so a core/x.c and a ui/x.c would not overwrite
        # each other's object here even if the check below let one through.
        o="$OUT/$(echo "${s%.c}" | tr / _).o"
        if ! cc "${flags[@]}" -c -o "$o" "$s"; then
            echo "FAIL  $s did not compile"

            exit 1
        fi
        objs+=("$o")
    done
}
CORE_OBJS=()
UI_OBJS=()
AED_OBJS=()
STUB_OBJS=()
compile CORE_CFLAGS CORE_OBJS "${CORE_SRCS[@]}"
compile CORE_CFLAGS STUB_OBJS "${STUB_SRCS[@]}"
compile UI_CFLAGS UI_OBJS "${UI_SRCS[@]}"
compile CFLAGS AED_OBJS "${AED_SRCS[@]}"
OBJS=("${CORE_OBJS[@]}" "${UI_OBJS[@]}" "${AED_OBJS[@]}" "${STUB_OBJS[@]}")

# A test is built at the lowest layer its includes allow. One that includes
# nothing but core headers is compiled as the core is and linked against the
# core objects and the stubs alone; one that stops at the UI, against the core
# and the UI. A core object that needed anything from above it, or a UI object
# that needed anything of AED's, fails that link -- which is the proof each is
# a library of its own, and why these tests are not simply linked against
# everything like the rest.
within() {    # within <test> <dirs...>: every include is in one of the dirs
    local t=$1 h d found
    shift
    for h in $(sed -n 's/^#include "\(.*\)"/\1/p' "$t"); do
        found=1
        for d in "$@"; do
            [ -f "src/$d/$h" ] && found=0
        done
        [ "$found" = 0 ] || return 1
    done

    return 0
}

core_tests=0
ui_tests=0
for t in test/test_*.c; do
    name=$(basename "$t" .c)
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        continue
    fi
    echo "=== $name ==="
    if within "$t" core; then
        core_tests=$((core_tests + 1))
        if ! cc "${CORE_CFLAGS[@]}" -o "$OUT/$name" "$t" \
                "${CORE_OBJS[@]}" "${STUB_OBJS[@]}"; then
            echo "FAIL  $name did not build against the core alone"
            status=1
            continue
        fi
    elif within "$t" core ui; then
        ui_tests=$((ui_tests + 1))
        if ! cc "${UI_CFLAGS[@]}" -o "$OUT/$name" "$t" \
                "${CORE_OBJS[@]}" "${UI_OBJS[@]}" "${STUB_OBJS[@]}"; then
            echo "FAIL  $name did not build against the core and the UI alone"
            status=1
            continue
        fi
    elif ! cc "${CFLAGS[@]}" -o "$OUT/$name" "$t" "${OBJS[@]}"; then
        echo "FAIL  $name did not compile"
        status=1
        continue
    fi
    "$OUT/$name" || status=$?
done

if [ -z "$FILTER" ]; then
    for layer in core ui; do
        n=$([ "$layer" = core ] && echo "$core_tests" || echo "$ui_tests")
        what=$([ "$layer" = core ] && echo "the core alone" \
                                   || echo "the core and the UI alone")
        if [ "$n" -gt 0 ]; then
            printf 'PASS  %-52s %d tests\n' "$layer tests link against $what" "$n"
        else
            echo "FAIL  no $layer tests: nothing proves the $layer stands alone"
            status=1
        fi
    done
fi

exit $status
