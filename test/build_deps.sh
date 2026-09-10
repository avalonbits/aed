#!/bin/bash
# Does the build rebuild what a changed header reaches?
#
# AgonDev's makefile has one rule, `obj/%.o: src/%.c`, and no header
# dependencies. Editing a header rebuilt nothing that included it, so adding a
# field to the middle of a struct in screen.h produced a binary in which half
# the objects read every field after it at the old offset. It linked, it was
# the right size, and the host suite passed -- test/run.sh compiles every source
# together on every run, so it cannot see a stale object at all.
#
# This checks the thing run.sh structurally cannot: that touching a header
# rebuilds precisely the objects whose sources include it, and that an
# incremental build is byte-identical to a clean one.
#
# Needs the AgonDev toolchain. Skipped, not failed, without it.
set -uo pipefail
cd "$(dirname "$0")/.."

if ! command -v agondev-config >/dev/null; then
    echo "SKIP  build_deps: agondev-config not on PATH"

    exit 0
fi

status=0
check() {
    if [ "$2" = "$3" ]; then
        printf 'PASS  %-52s %s\n' "$1" "$2"
    else
        printf 'FAIL  %-52s got %s, want %s\n' "$1" "$2" "$3"
        status=1
    fi
}

make clean >/dev/null 2>&1
make >/dev/null 2>&1 || { echo "FAIL  build_deps: clean build failed"; exit 1; }
cp bin/aed.bin /tmp/aed_deps_clean.$$ 2>/dev/null

# Every source that reaches screen.h must be recompiled -- including the ones
# that reach it through a header of their own, which is most of them. The set is
# worked out here by following the includes, rather than read back out of the .d
# files, so that this is checking make against an independent answer and not
# against its own bookkeeping.
want=$(python3 - <<'PYEOF'
import os, re

INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

def includes(path):
    try:
        return INC.findall(open(path).read())
    except OSError:
        return []

def reaches(start, target):
    seen, stack = set(), list(includes(start))
    while stack:
        h = stack.pop()
        if h in seen:
            continue
        seen.add(h)
        if h == target:
            return True
        stack += includes(os.path.join('src', h))

    return False

names = [f[:-2] for f in sorted(os.listdir('src'))
         if f.endswith('.c') and reaches(os.path.join('src', f), 'screen.h')]
print(' '.join(names) + ' ')
PYEOF
)
touch src/screen.h
got=$(make 2>&1 | sed -n 's/^\[compiling src\/\(.*\)\.c\]$/\1/p' | sort | tr '\n' ' ')
check "a changed header rebuilds every source including it" "$got" "$want"

# And the bug itself: a field added to the middle of a struct, which moves the
# offset of every field after it. Built incrementally this must produce the same
# binary as building it clean. Without header dependencies it does not -- the
# objects that were not recompiled go on using the old offsets, and that is
# precisely what shipped.
#
# The header is restored on the way out however this exits.
# The header goes back and the tree is rebuilt from it however this exits:
# bin/aed.bin is tracked, and leaving it built from a header that no longer
# exists would show up as a modified file and could be committed by accident.
cp src/screen.h /tmp/aed_deps_hdr.$$
restore() {
    mv -f /tmp/aed_deps_hdr.$$ src/screen.h 2>/dev/null
    rm -f /tmp/aed_deps_clean.$$ /tmp/aed_deps_inc.$$
    make clean >/dev/null 2>&1
    make >/dev/null 2>&1
}
trap restore EXIT

sed -i 's/^    char tab_size_;$/    char deps_probe_;\n    char tab_size_;/' src/screen.h
make >/dev/null 2>&1
cp bin/aed.bin /tmp/aed_deps_inc.$$
make clean >/dev/null 2>&1
make >/dev/null 2>&1

if cmp -s /tmp/aed_deps_inc.$$ bin/aed.bin; then
    printf 'PASS  %-52s %s\n' "a struct field added: incremental == clean" "identical"
else
    printf 'FAIL  %-52s %s\n' "a struct field added: incremental == clean" "STALE OBJECTS"
    status=1
fi

exit $status
