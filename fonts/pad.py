#!/usr/bin/env python3
"""Pad a font with blank rows, to separate the lines of text it draws.

An 8x8 font has nowhere to put a gap between lines: descenders land on the last
row, so `p` and `y` touch the capitals underneath them. The VDP's cell height is
a plain parameter rather than a fixed eight, so the way out is to make the cell
taller than the glyph -- eight rows of glyph and two blank ones is 8x10, which
divides 480 exactly and gives 48 rows instead of 60.

    pad.py unscii8.bin unscii8x10.bin 2

The blank rows go below the glyph. Where they go makes no difference to the
spacing between lines -- the gap is the same either way -- but it does decide
where the glyph sits inside the cursor block and the selection highlight, and
sitting at the top of the block reads better than floating in the middle.

Fonts are raw: 256 glyphs, 8 pixels wide, one byte per row, no header. The
height is the file size divided by 256, which is what AED uses to work it out.
"""
import sys

GLYPHS = 256


def pad(data: bytes, extra: int) -> bytes:
    if len(data) % GLYPHS:
        raise SystemExit(f"{len(data)} bytes is not {GLYPHS} glyphs of whole rows")
    height = len(data) // GLYPHS
    out = bytearray()
    for c in range(GLYPHS):
        out += data[c * height:(c + 1) * height] + bytes(extra)

    return bytes(out)


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} <in.bin> <out.bin> <blank rows>")
    src, dst, extra = sys.argv[1], sys.argv[2], int(sys.argv[3])
    with open(src, "rb") as f:
        data = f.read()
    out = pad(data, extra)
    with open(dst, "wb") as f:
        f.write(out)
    print(f"{dst}: {len(out)} bytes, {len(out) // GLYPHS} rows per glyph")


if __name__ == "__main__":
    main()
