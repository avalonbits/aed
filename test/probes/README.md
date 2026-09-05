Standalone probes for things the host tests cannot reach. Not part of the build:
copy one into an AgonDev project with `NAME = <probe>` and run it on the device.

- `kmap.c` — dumps the MOS virtual keyboard map live, 16 bytes of hex, plus
  `keymods` and `keyascii`. Shows what the hardware reports while keys are held.
- `kscan.c` — asks for each navigation key in turn and prints where it sits in
  that map. The published table does not match real keyboards; measure with
  this instead.

See `.internal/docs/KEYBOARD.md` for what they were written to investigate.
- `lastcol.c` — writes one character in a chosen column, over and over, and
  counts key events beside it. Hold CTRL+SHIFT and tap an arrow: if the counter
  keeps up, the writes are harmless; if it freezes until the keys are released,
  the write is arming the VDP's CTRL+SHIFT pause. `COL_FROM_RIGHT` picks the
  column, so building it twice gives two programs differing in one column.

  Measured on VDP 2.16.0 / MOS 3.0.2, six taps with the chord held: writing the
  last column of the screen freezes the counter and it catches up to 8 on
  release; writing the column before it shows 8 while the keys are still down.
  That is the evidence for the reserved column in `scr_init`.
- `kbev.c` — prints every `agon/keyboard.h` event (ascii, kmod, vkey, up/down)
  as it arrives. This is the input path AED should use; the other two probes
  are from the superseded keyboard-map investigation.
