#!/bin/bash
# Are any stack frames past the size the eZ80 can address cheaply?
#
# `(IX + d)` takes a signed byte, so a local more than 128 bytes into a frame
# cannot be reached with one instruction. The compiler computes its address
# instead -- `ld bc, -139; lea hl, ix + 0; add hl, bc; ld hl, (hl)` -- and pays
# that on *every* access to *every* local past the boundary. Nothing in the C
# says it is happening, and the usual way it arrives is a 256-byte buffer added
# to a function that was fine, or a cold helper with one being inlined into a
# caller that was fine.
#
# This reads the generated assembly and fails when a frame crosses the line.
# Both numbers below come from a build, not from a guess: raise one only with a
# measurement that says the frame is worth it.
#
# Needs the AgonDev toolchain. Skipped, not failed, without it.
set -uo pipefail
cd "$(dirname "$0")/.."

if ! command -v agondev-config >/dev/null; then
    echo "SKIP  frames: agondev-config not on PATH"

    exit 0
fi

TOOLCHAIN=${AGONDEV_TOOLCHAIN:-$HOME/agondev}
CC=$TOOLCHAIN/bin/ez80-none-elf-clang
if [ ! -x "$CC" ]; then
    echo "SKIP  frames: $CC not found"

    exit 0
fi

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# The flags the real build uses, from AgonDev's makefile.inc. -Oz matters: the
# optimisation level decides what gets inlined, and inlining is what puts a cold
# function's buffer in a hot function's frame.
for f in src/*.c; do
    "$CC" -mllvm -z80-gas-style -mllvm -z80-print-zero-offset -nostdinc \
          -Iinclude -isystem "$TOOLCHAIN/include" -target ez80-none-elf \
          -DAGONDEV -Oz -Wa,-march=ez80+full -fno-threadsafe-statics \
          -S "$f" -o "$OUT/$(basename "$f" .c).s" 2>/dev/null \
        || { echo "FAIL  frames: could not compile $f"; exit 1; }
done

exec python3 - "$OUT" <<'PYEOF'
import glob, os, re, sys

# main holds the editor itself, which has to live somewhere and is reached
# through a pointer everywhere else -- main's own two accesses are all it costs.
# Anything else on this list needs a reason next to it.
ALLOW = {'main': 800}

# Frame escapes left in the program. A budget rather than zero because main's
# are real and there is no point pretending otherwise.
ESCAPE_BUDGET = 2

frames, escapes, cur, pend = {}, {}, None, None
for path in sorted(glob.glob(os.path.join(sys.argv[1], '*.s'))):
    unit = os.path.basename(path)[:-2]
    for line in open(path):
        s = line.strip()
        m = re.match(r'^_([A-Za-z_][A-Za-z0-9_]*):', s)
        if m:
            cur, pend = (unit, m.group(1)), None
            frames.setdefault(cur, 0)
            continue
        if cur is None:
            continue
        m = re.match(r'^ld\s+hl,\s*(-\d+)', s)
        if m:
            pend = -int(m.group(1))
        elif 'call' in s and '__frameset' in s and '__frameset0' not in s:
            if pend is not None:
                frames[cur] = max(frames[cur], pend)
        elif re.search(r'lea\s+hl,\s*ix \+ 0', s):
            escapes[cur] = escapes.get(cur, 0) + 1

status = 0
over = [(u, n, sz) for (u, n), sz in frames.items()
        if sz >= 128 and sz > ALLOW.get(n, 0)]
for unit, name, size in sorted(over, key=lambda t: -t[2]):
    print('FAIL  %-52s %d bytes' % ('frame over 128: %s.c:%s' % (unit, name), size))
    status = 1
if not over:
    biggest = max((sz, u, n) for (u, n), sz in frames.items() if n not in ALLOW)
    print('PASS  %-52s %d bytes (%s.c:%s)'
          % ('every frame inside the ix displacement', biggest[0], biggest[1], biggest[2]))

total = sum(escapes.values())
if total > ESCAPE_BUDGET:
    for (unit, name), n in sorted(escapes.items(), key=lambda kv: -kv[1])[:8]:
        print('      %s.c:%s has %d' % (unit, name, n))
    print('FAIL  %-52s %d, budget %d' % ('frame escapes', total, ESCAPE_BUDGET))
    status = 1
else:
    print('PASS  %-52s %d, budget %d' % ('frame escapes', total, ESCAPE_BUDGET))

sys.exit(status)
PYEOF
