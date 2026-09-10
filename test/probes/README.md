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
- `font9.c` — uploads a font of a chosen height and reports the screen geometry
  before and after. Written to answer whether the VDP accepts a cell height that
  is not a multiple of 8, which decides whether a blank separator row between
  text lines can be had without redrawing a font.

  It can. Measured on VDP 2.16.0 / MOS 3.0.2 with an 8x9 build of the stock
  font: 80x60 before, 80x53 after, and the VDP logs `Created text cursor bitmap
  8x9`. Declaring the wrong height for the same buffer is rejected with
  `createFontFromBuffer: buffer 100 is not the correct size`, so the height is
  genuinely honoured rather than stored and ignored.

  **A font change raises `vdp_pflag_mode` (0x10) even when it failed**, because
  `FONT_SELECT` calls `sendModeInformation()` before knowing the outcome. Waiting
  on the flag says the VDP replied, not that the font took; check the row count.

  Runs without a display: the emulator has no headless flag and Xvfb is not
  installed here, but `SDL_VIDEODRIVER=dummy` works, and `log_word` smuggles
  numbers to the host through the VDP's debug log, which `--verbose` surfaces.
  See `.internal/docs/FONTS.md`.
- `fonttest.c` — loads a font and shows the result, so a person can look at it.
  Takes the path as an argument, so several can be compared without editing the
  settings file:

      fonttest /config/aed/unscii8x9.bin

  Does what `scr_load_font` does and puts the workings on screen: whether the
  VDP has the font API at all, the height the file implies, the measured ascent,
  whether the mode packet came back, and the geometry afterwards. The sample
  puts descenders directly above capitals, which is the comparison that matters
  when choosing a height -- a 9- or 10-row font has a blank row between the two
  and an 8-row font does not.

  **A pass does not mean the font is correct.** The read-back asks the VDP to
  match the screen against the font it is holding, so a font uploaded at the
  wrong offset agrees with itself and reads back perfectly while the screen
  shows nonsense. "renders" means glyphs are reaching the screen; whether they
  are the *right* glyphs is what the sample text is for.

  It reported everything healthy throughout the hunt for #109, and was right to:
  the editor's blank screen was a stale object file, not the font path.
