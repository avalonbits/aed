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

    # Likewise the unpadded fonts and build.py, from the unscii sources.
    for pair in "unscii-8.hex unscii8.bin" "unscii-16.hex unscii16.bin"; do
        set -- $pair
        python3 fonts/build.py "fonts/src/$1" "$tmp" >/dev/null
        if cmp -s "$tmp" "fonts/$2"; then
            printf 'PASS  %-52s %s\n' "$2 is build.py's output" "identical"
        else
            printf 'FAIL  %-52s %s\n' "$2 is build.py's output" "differs"
            status=1
        fi
    done

    # A font swaps for the stock one only if every byte means the same
    # character in both. These are slots where Latin-1 -- unscii's own order --
    # and the stock font disagree, plus one where they agree.
    PYTHONDONTWRITEBYTECODE=1 python3 - <<'EOF' || status=1
import sys
sys.path.insert(0, "fonts")
from build import charmap

want = {
    0x12: 0x250C,  # box drawing, a control code in Latin-1
    0x1C: 0x2588,  # full block
    0x7F: 0x00A9,  # copyright sign, delete in Latin-1
    0x80: 0x20AC,  # euro
    0x81: 0x25A0,  # teletext block, unassigned in Windows-1252
    0x84: 0x201E,  # double low quote
    0x97: 0x2014,  # em dash
    0x9D: 0x2016,  # teletext double bar
    0xC4: 0x00C4,  # A diaeresis, the same in both
}
cps = charmap()
bad = {f"{s:#04x}": f"U+{cps[s]:04X}" for s, cp in want.items() if cps[s] != cp}
if len(cps) != 256 or bad:
    print(f"FAIL  {'the fonts follow the stock character map':52} {len(cps)} slots, wrong: {bad}")
    sys.exit(1)
print(f"PASS  {'the fonts follow the stock character map':52} {len(want)} slots")
EOF
fi

exit $status
