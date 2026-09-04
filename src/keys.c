/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "keys.h"

#include <agon/keyboard.h>

// On AgonDev's key handler, and why AED uses it anyway.
//
// MOS calls the key vector from inside its UART0 interrupt handler, with DE
// holding the address of the four-byte VDP keyboard packet. An interrupt
// handler is supposed to give every register back as it found it, and
// <agon/keyboard.h>'s does not. Disassembled from libagon.a:
//
//     kbuf_event_handler:  push af / push bc / push hl
//                          call kbuf_append
//                          pop hl / pop bc / pop af / ret
//
//     kbuf_append:         ... ex de,hl / ld bc,4 / ldir
//
// DE is not saved, and the ex+ldir leaves it pointing four bytes into AED's own
// ring buffer -- not merely advanced within MOS's packet, but somewhere else
// entirely.
//
// It does not matter, and that is worth writing down because the obvious
// reading says it should. MOS never reads DE after the vector returns. From
// vdp_protocol.asm, vdp_protocol_KEY passes the packet address in DE, and on
// the way back reloads every field absolutely:
//
//     LD A, (_vdp_protocol_data + 0)    ; ASCII
//     LD A, (_vdp_protocol_data + 1)    ; modifiers
//     ...then sets B and C itself before JP keyboard_handler
//
// keyboard_handler takes B and C and loads its own DE. So the clobber is a
// contract violation with nothing downstream of it.
//
// A replacement vector in assembly was written and measured (branch
// fix/own-key-vector). It is not merged: it puts hand-written code inside an
// interrupt handler, where a mistake hangs the machine, to fix something no
// caller can observe. Checked against MOS 3.x; the vector call has had this
// shape since 2023.

// Room for a burst without dropping any. The longest one AED can provoke is a
// chord being released -- CTRL+SHIFT+RIGHT alone is six events, down and up for
// three keys -- and holding a key repeats faster than a slow repaint returns
// here. 32 events is 128 bytes.
#define KEYS_EVENTS 32

void keys_open(void) {
    // kbuf_init cannot report failure, and its allocation is 128 bytes made
    // after the document buffer and the clipboard have already succeeded. If
    // that fails there is no editor to run anyway.
    kbuf_init(KEYS_EVENTS);
}

void keys_close(void) {
    kbuf_deinit();
}

key_press keys_wait(void) {
    struct keyboard_event_t e;

    for (;;) {
        if (!kbuf_poll_event(&e)) {
            continue;
        }
        if (!e.isdown) {
            continue;
        }
        // A modifier going down is not a keystroke. MOS reports one for every
        // shift or control press, and the editor does nothing with them --
        // what a chord means is carried in the modifier bits of the key it
        // modifies. Returning them anyway costs a full turn of the event loop,
        // repaint and all, for each one, and the editor is not reading the
        // queue while it does that.
        if (e.vkey >= VK_LSHIFT && e.vkey <= VK_RGUI) {
            continue;
        }

        key_press kp;
        kp.ch = (char) e.ascii;
        kp.vkey = (VKey) e.vkey;
        kp.mods = (char) e.kmod;

        return kp;
    }
}
