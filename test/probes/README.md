Standalone probes for things the host tests cannot reach. Not part of the build:
copy one into an AgonDev project with `NAME = <probe>` and run it on the device.

- `kmap.c` — dumps the MOS virtual keyboard map live, 16 bytes of hex, plus
  `keymods` and `keyascii`. Shows what the hardware reports while keys are held.
- `kscan.c` — asks for each navigation key in turn and prints where it sits in
  that map. The published table does not match real keyboards; measure with
  this instead.

See `.internal/docs/KEYBOARD.md` for what they were written to investigate.
- `kbev.c` — prints every `agon/keyboard.h` event (ascii, kmod, vkey, up/down)
  as it arrives. This is the input path AED should use; the other two probes
  are from the superseded keyboard-map investigation.
- `arrows.c` — moves a marker with the arrow keys and counts what arrives,
  alongside MOS's own packet counter. Written to answer whether the platform
  delivers arrow keys while CTRL and SHIFT are held. It does: 4/4, where AED
  managed 1/4. This is the control the investigation needed.
