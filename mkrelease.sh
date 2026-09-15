#!/bin/bash
# Builds the release zip: unzip it at the root of an SD card and everything
# lands where AED expects it.
#
#   bin/aed.bin              MOS searches /bin, so `aed` works as a command
#   config/aed/unscii8.bin   the three fonts, where the README's example
#   config/aed/unscii8x10.bin  settings file points at them
#   config/aed/unscii16.bin
#   config/aed/syntax/*.cfg  a grammar per language, read when a file is opened
#   config/aed/themes/*.cfg  a theme per background, read with the grammar
#
# There is deliberately no config/aed.ini in here. AED writes that itself on
# first run, from the colours the machine is already using, and shipping one
# would overwrite whatever the user had. The fonts stay inert until a settings
# file names one.
#
# Usage: ./mkrelease.sh [version]   (default: the version AED reports)
set -euo pipefail
cd "$(dirname "$0")"

VERSION=${1:-$(sed -n 's/.*AED_VERSION "\(.*\)".*/\1/p' src/version.h)}
if [ -z "$VERSION" ]; then
    echo "mkrelease: no version given and none found in src/version.h" >&2

    exit 1
fi

if ! command -v zip >/dev/null; then
    echo "mkrelease: zip is not installed" >&2

    exit 1
fi

# Built fresh, not whatever is lying in bin/. A release made from a stale
# binary is the kind of mistake that is only found by a user.
if command -v agondev-config >/dev/null; then
    make >/dev/null
else
    echo "mkrelease: WARNING agondev-config not on PATH; using the existing bin/aed.bin" >&2
fi

if [ ! -f bin/aed.bin ]; then
    echo "mkrelease: bin/aed.bin does not exist" >&2

    exit 1
fi

# The version in the binary has to be the version on the tin.
REPORTED=$(sed -n 's/.*AED_VERSION "\(.*\)".*/\1/p' src/version.h)
if [ "$REPORTED" != "$VERSION" ]; then
    echo "mkrelease: src/version.h says $REPORTED but the zip would say $VERSION" >&2
    echo "           the banner and the help screen would be lying; fix one of them" >&2

    exit 1
fi

OUT="aed-$VERSION.zip"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/bin" "$STAGE/config/aed"
cp bin/aed.bin "$STAGE/bin/"
cp fonts/unscii8.bin fonts/unscii8x10.bin fonts/unscii16.bin "$STAGE/config/aed/"

# Grammars and themes. Unlike the fonts these are not inert: AED looks in these
# two directories every time a file is opened, so leaving them out of the zip
# would ship syntax highlighting that never highlights anything.
mkdir -p "$STAGE/config/aed/syntax" "$STAGE/config/aed/themes"
cp config/aed/syntax/*.cfg "$STAGE/config/aed/syntax/"
cp config/aed/themes/*.cfg "$STAGE/config/aed/themes/"

rm -f "$OUT"
(cd "$STAGE" && zip -q -r -X "$OLDPWD/$OUT" .)

echo "$OUT"
unzip -l "$OUT" | sed -n '4,$p' | head -n -2

# The release page carries the loose files too, so somebody who already has the
# fonts can take just the binary -- which is what most upgrades are. The zip is
# still the one to point people at.
echo
echo "attach all of these to the release:"
for f in "$OUT" bin/aed.bin fonts/unscii8.bin fonts/unscii8x10.bin fonts/unscii16.bin; do
    echo "  $f"
done
