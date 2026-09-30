#!/usr/bin/env python3
"""Build an Agon font from an unscii .hex source, laid out like the stock font.

A byte on the Agon is not a Unicode code point. The VDP's stock 8x8 font puts
Windows-1252 in 128-255, a handful of teletext graphics in the five slots
Windows-1252 leaves empty, a copyright sign at 127, and box drawing, shades and
card suits in 0-31. unscii is indexed by Unicode, so its first 256 glyphs are
Latin-1 -- control codes where the Agon has quotes, dashes and the euro, and
nothing where it has line drawing. Text written for the stock font comes out
as the wrong characters.

So each slot here takes unscii's glyph for the character the stock font draws
in that slot, and the result swaps for the stock font char for char:

    build.py src/unscii-8.hex unscii8.bin
    build.py src/unscii-16.hex unscii16.bin

The height comes from the source: 8x8 glyphs are 16 hex digits, 8x16 are 32.
Double-width glyphs are skipped; the VDP font is 8 pixels wide.
"""
import sys

GLYPHS = 256

# 0-31 in the stock font. Each is what its glyph draws, checked against the
# bitmap rather than the VDP's comments, several of which say what the slot
# "should be" instead.
LOW = [
    0x25CB, 0x25AA, 0x2665, 0x2666, 0x2663, 0x2660, 0x2551, 0x2550,
    0x2554, 0x2557, 0x255A, 0x255D, 0x2591, 0x2592, 0x25BA, 0x25C4,
    0x2502, 0x2500, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524,
    0x2534, 0x252C, 0x2193, 0x253C, 0x2588, 0x2584, 0x2580, 0x25AC,
]

# The slots where the stock font is neither ASCII nor Windows-1252.
OVERRIDES = {
    0x7F: 0x00A9,  # copyright sign, where ASCII has delete
    0x81: 0x25A0,  # teletext block
    0x8D: 0x2191,  # teletext up arrow
    0x8F: 0x2190,  # teletext left arrow
    0x90: 0x2192,  # teletext right arrow
    0x9D: 0x2016,  # teletext double bar
}


def lowered(glyph: bytes, like: bytes) -> bytes:
    """`glyph` moved down until its last inked row is level with `like`'s."""
    def last(g: bytes) -> int:
        return max(i for i, row in enumerate(g) if row)

    shift = last(like) - last(glyph)

    return bytes(shift) + glyph[:len(glyph) - shift]


def one_chevron(glyph: bytes) -> bytes:
    """The right-hand chevron of a double angle quote, moved to the middle."""
    return bytes(((row & 0x0F) << 2) & 0xFF for row in glyph)


# Four Windows-1252 quotes unscii does not have, made from the ones it does, so
# every glyph is in unscii's hand: the low quotes sit on the comma's baseline,
# which is what makes them low, and the single angle quotes are half a double.
DERIVED = {
    0x201A: lambda g: g[0x002C],                      # single low-9 quote
    0x201E: lambda g: lowered(g[0x201D], g[0x002C]),  # double low-9 quote
    0x2039: lambda g: one_chevron(g[0x00AB]),         # single left angle quote
    0x203A: lambda g: one_chevron(g[0x00BB]),         # single right angle quote
}


def charmap() -> list[int]:
    """The Unicode code point the stock font draws in each of the 256 slots."""
    cps = list(LOW)
    for b in range(32, GLYPHS):
        if b in OVERRIDES:
            cps.append(OVERRIDES[b])
        else:
            cps.append(ord(bytes([b]).decode("cp1252")))

    return cps


def load(path: str) -> dict[int, bytes]:
    glyphs = {}
    with open(path) as f:
        for line in f:
            cp, bits = line.strip().split(":")
            glyphs[int(cp, 16)] = bytes.fromhex(bits)
    for cp, make in DERIVED.items():
        glyphs.setdefault(cp, make(glyphs))

    return glyphs


def build(glyphs: dict[int, bytes]) -> bytes:
    # The narrow glyphs all have the same length; the wide ones are twice it.
    height = min(len(g) for g in glyphs.values())
    out = bytearray()
    for slot, cp in enumerate(charmap()):
        g = glyphs.get(cp)
        if g is None or len(g) != height:
            raise SystemExit(f"slot {slot:#04x}: no {height}-row glyph for U+{cp:04X}")
        out += g

    return bytes(out)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} <unscii.hex> <out.bin>")
    out = build(load(sys.argv[1]))
    with open(sys.argv[2], "wb") as f:
        f.write(out)
    print(f"{sys.argv[2]}: {len(out)} bytes, {len(out) // GLYPHS} rows per glyph")


if __name__ == "__main__":
    main()
