#!/bin/bash
# Packages AED's two libraries for another program to build against:
#
#   lib/libedcore.a   the document -- see src/core
#   lib/libedui.a     views, prompts, commands and the loop -- see src/ui
#   include/core/*.h  the core's headers
#   include/ui/*.h    the UI's, which include the core's by name
#   VERSION           the AED version and commit they were built from
#
# Built with -Iinclude/core -Iinclude/ui and linked with -ledui -ledcore (in
# that order: the UI uses the core). mkrelease.sh attaches the result to each
# release as aed-libs-<version>.tar.gz; test/libs.sh builds against it.
#
# Usage: ./mklibs.sh [version] [output directory]   (defaults: src/version.h, .)
set -euo pipefail
cd "$(dirname "$0")"

VERSION=${1:-$(sed -n 's/.*AED_VERSION "\(.*\)".*/\1/p' src/version.h)}
DEST=${2:-.}
if [ -z "$VERSION" ]; then
    echo "mklibs: no version given and none found in src/version.h" >&2

    exit 1
fi
if ! command -v agondev-config >/dev/null; then
    echo "mklibs: agondev-config not on PATH" >&2

    exit 1
fi

# Built fresh, for the same reason mkrelease.sh builds the binary fresh.
make libs >/dev/null

NAME="aed-libs-$VERSION"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/$NAME/lib" "$STAGE/$NAME/include/core" "$STAGE/$NAME/include/ui"
cp bin/libedcore.a bin/libedui.a "$STAGE/$NAME/lib/"
# The core's internal header stays home: text_buffer_int.h is what the six
# text_buffer_*.c files share with each other, and nothing outside them uses it.
for h in src/core/*.h; do
    case "$h" in *_int.h) continue ;; esac
    cp "$h" "$STAGE/$NAME/include/core/"
done
cp src/ui/*.h "$STAGE/$NAME/include/ui/"
{
    echo "aed $VERSION"
    echo "commit $(git rev-parse HEAD 2>/dev/null || echo unknown)"
} > "$STAGE/$NAME/VERSION"

OUT="$DEST/$NAME.tar.gz"
tar -C "$STAGE" -czf "$OUT" "$NAME"
echo "$OUT"
