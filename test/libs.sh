#!/bin/bash
# The libraries, as AED is linked against them and as another program gets them.
#
# 1. aed.bin comes from the two archives. The Makefile links AED's own objects
#    with -ledui -ledcore rather than every object there is, by overriding what
#    AgonDev's makefile.inc links. If a change to makefile.inc ever made that
#    override stop taking, the link would quietly go back to every object and
#    still work -- so the link map is read: members of both archives, and no
#    core or UI object linked on its own.
#    `make lib`, AgonDev's name for building a library, builds the two.
# 2. The package mklibs.sh builds is enough to build with. Two small programs
#    are built against it and nothing else: one on the core alone, one that
#    starts an editor through the UI. Each links only if its archive has
#    everything it needs from the layers under it and nothing from above.
#
# Needs the AgonDev toolchain. Skipped, not failed, without it.
set -uo pipefail
cd "$(dirname "$0")/.."

if ! command -v agondev-config >/dev/null; then
    echo "SKIP  libs: agondev-config not on PATH"

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

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

# --- 1. the link map ---
# Linked afresh: an incremental make relinks only when an object or archive
# changed, so a change to how the link is done would otherwise leave the map
# from the last one standing.
rm -f bin/aed.noname.bin
make >/dev/null 2>&1 || { echo "FAIL  libs: the build failed"; exit 1; }
check "aed.bin links members of libedcore.a" \
      "$(grep -c 'libedcore\.a(' bin/aed.map | sed 's/^0$/none/; s/^[1-9].*$/some/')" some
check "aed.bin links members of libedui.a" \
      "$(grep -c 'libedui\.a(' bin/aed.map | sed 's/^0$/none/; s/^[1-9].*$/some/')" some
check "  and no core or UI object on its own" \
      "$(grep -cE 'obj/(core|ui)/' bin/aed.map)" 0

# AgonDev's `make lib` builds the two libraries too.
rm -f bin/libedcore.a bin/libedui.a
make lib >/dev/null 2>&1
check "make lib builds both libraries" \
      "$(ls bin/libedcore.a bin/libedui.a 2>/dev/null | wc -l | tr -d ' ')" 2
make libs >/dev/null 2>&1

# --- 2. the package ---
PKG_TGZ=$(./mklibs.sh 0.0.0-test "$W" 2>/dev/null) \
    || { echo "FAIL  libs: mklibs.sh failed"; exit 1; }
tar -C "$W" -xzf "$PKG_TGZ"
PKG="$W/aed-libs-0.0.0-test"
check "the package holds both archives" \
      "$(ls "$PKG/lib" | tr '\n' ' ')" "libedcore.a libedui.a "
check "  and no internal header" "$(ls "$PKG/include/core" | grep -c '_int\.h')" 0
want="$(git rev-parse HEAD)"
git diff --quiet HEAD || want="$want-dirty"
check "  and names the commit it was built from" \
      "$(sed -n 's/^commit //p' "$PKG/VERSION")" "$want"

# build <name> <libs> <source>: an AgonDev project using only the package.
build() {
    local d="$W/$1"
    mkdir -p "$d/src"
    printf '%s\n' "$3" > "$d/src/main.c"
    cat > "$d/Makefile" <<MK
NAME=$1
include \$(shell agondev-config --makefile)
CFLAGS += -I$PKG/include/core -I$PKG/include/ui
PROJECTLIBDIR := $PKG/lib
LIBS := $2
MK
    (cd "$d" && make >/dev/null 2>&1) && [ -f "$d/bin/$1.bin" ] && echo built || echo "did not build"
}

check "a program on the core alone builds against the package" "$(build coreonly -ledcore '
#include "app.h"
#include "text_buffer.h"
static const app_context APP = { .name = "check" };
int main(void) {
    static text_buffer tb;
    app_set(&APP);
    if (tb_init(&tb, 4, NULL) == NULL) {
        return 1;
    }
    tb_destroy(&tb);

    return 0;
}')" built

check "a program starting an editor builds against it" "$(build withui '-ledui -ledcore' '
#include "app.h"
#include "editor.h"
static const app_context APP = { .name = "check" };
static const ed_program PROG = { &APP, NULL, NULL, NULL, NULL };
int main(void) {
    static editor ed;
    if (ed_init_for(&ed, 8, NULL, &PROG) == NULL) {
        return 1;
    }
    ed_run(&ed);
    ed_destroy(&ed);

    return 0;
}')" built

exit $status
