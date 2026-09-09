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
- `sdrate.c` — times bulk reads and writes, 1 KiB against 4 KiB operations, and
  ascending against descending seeks. Written for the paging design in
  `.internal/docs/PAGING.md`, which needs all three: throughput sets how long a
  save takes, per-operation overhead sets the floor on the chunk size, and the
  seek ratio says whether scrolling up costs more than scrolling down.

  **Run it on hardware.** On the emulator with `--sdcard <path>` the host
  filesystem serves the calls and MOS's FatFS does not appear to run at all:
  reads and seeks both measure 0 cs, and two identical runs gave 673 and
  1706 KiB/s for writes. The one thing that does come through is that the eZ80
  side is not the bottleneck — with MOS and the C code in the loop throughout,
  writes still exceeded 1.5 MiB/s. Whatever the real rate is, the card sets it.

  A raw image (`--sdcard-img`) would make FatFS run for real and would answer
  the seek and overhead questions, but still not throughput. It needs
  `dosfstools` and `mtools`, neither of which is installed here.
- `kbev.c` — prints every `agon/keyboard.h` event (ascii, kmod, vkey, up/down)
  as it arrives. This is the input path AED should use; the other two probes
  are from the superseded keyboard-map investigation.
