#!/bin/bash
# The shipped fonts, checked against what AED will make of them.
#
# AED takes a font's height from its file size divided by 256 -- there is no
# header to say otherwise -- so a file that is not a whole number of 256-byte
# rows is not a font, and one whose size is wrong is a font of the wrong height
# with no way to notice. A truncated or mangled commit of a binary asset is
# exactly the sort of thing nothing else here would catch.
set -uo pipefail
cd "$(dirname "$0")/.."

status=0
check() {
    if [ "$2" = "$3" ]; then
        printf 'PASS  %-52s %s\n' "$1" "$2"
    else
        printf 'FAIL  %-52s got %s, want %s\n' "$1" "$2" "$3"
        status=1
    fi
}

for f in fonts/*.bin; do
    size=$(stat -c%s "$f")
    check "$(basename "$f") is a whole number of glyph rows" "$((size % 256))" "0"
    rows=$((size / 256))
    # Four rows is the fewest the editor will take (header, footer, and
    # something between them); 255 is the most the create command can carry.
    if [ "$rows" -ge 4 ] && [ "$rows" -le 255 ]; then
        printf 'PASS  %-52s %s rows\n' "  and a height AED will accept" "$rows"
    else
        printf 'FAIL  %-52s %s rows\n' "  and a height AED will accept" "$rows"
        status=1
    fi
done

# The padded font must be exactly what pad.py makes of the unpadded one, so the
# two cannot drift apart and the shipped binary is reproducible rather than a
# file nobody can regenerate.
if command -v python3 >/dev/null; then
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT
    python3 fonts/pad.py fonts/unscii8.bin "$tmp" 2 >/dev/null
    if cmp -s "$tmp" fonts/unscii8x10.bin; then
        printf 'PASS  %-52s %s\n' "unscii8x10.bin is pad.py's output" "identical"
    else
        printf 'FAIL  %-52s %s\n' "unscii8x10.bin is pad.py's output" "differs"
        status=1
    fi
fi

exit $status
