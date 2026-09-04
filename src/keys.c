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
