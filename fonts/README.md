# Fonts

Drop one on the card and name it in the settings file:

```ini
[editor]
font = /config/aed/unscii8x10.bin
```

Nothing is sent unless that setting is present, and AED asks the VDP whether it
has the font API before uploading anything, so setting it on an older VDP costs
you the stock font rather than a wrecked screen. See the `font` section of the
main README.

| file | cell | screen | |
|---|---|---|---|
| `unscii8.bin` | 8x8 | 80x60 | unscii as published. Better letterforms than the stock font, but the lines still touch. |
| `unscii8x10.bin` | 8x10 | 80x48 | the same, with two blank rows added. Two rows of gap between lines. |
| `unscii16.bin` | 8x16 | 80x30 | unscii-16, drawn at sixteen rows. Needs no padding: descenders end on row 15 and capitals start on row 2, so the gap is already there. |

## Why the padded one exists

An 8x8 font has nowhere to put a gap. Descenders land on the last row, so `p`
and `y` touch the capitals on the line below, and every mechanical way of
freeing a row wrecks the glyphs -- folding the top row flat-tops every round
capital, and dropping a duplicate interior row leaves 68 of 256 glyphs untouched
and lopsides the rest.

The way out is that the VDP's cell height is a **parameter**, not a fixed eight.
Making the cell taller than the glyph costs rows, not columns: eighty columns
either way. Ten divides 480 exactly, so there is no dead strip at the bottom;
nine leaves three pixels over.

`pad.py` produces it:

```
python3 pad.py unscii8.bin unscii8x10.bin 2
```

## Provenance

unscii, by Viznut, from <http://viznut.fi/unscii/>. **Public domain (CC0)**,
stated in the font sources themselves.

These are the first 256 code points of `unscii-8.hex` and `unscii-16.hex`,
converted to the raw form the VDP wants: 256 glyphs, 8 pixels wide, one byte per
row, no header, lowest character first. The height is the file size divided by
256, which is how AED works it out -- there is nothing in the file to say.

`unscii-8-tall.hex` is deliberately not here. It is `unscii-8` with every row
emitted twice -- verified byte for byte, all 256 glyphs -- so it is 8x16 in cell
size and 8x8 in detail, every stroke doubled in thickness. `unscii16.bin` is the
one actually drawn at sixteen rows.
