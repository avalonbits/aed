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

#include "input.h"

#include <agon/mos.h>

key_event input_read(void) {
    // The counter the VDP bumps for every key packet. Held between calls so a
    // packet that arrived while the editor was busy repainting is still picked
    // up rather than missed.
    //
    // Deliberately not seeded to the counter's current value at startup. The
    // last packet before the editor runs is the release of whatever RETURN
    // launched it, and releases are skipped below -- so the first thing the
    // editor waits for is a genuine keypress either way, and not seeding keeps
    // this a plain loop with nothing that only happens once.
    static uint8_t seen = 0;

    key_event ev;
    for (;;) {
        const uint8_t count = getsysvar_vkeycount();
        if (count == seen) {
            continue;
        }
        seen = count;

        if (!getsysvar_vkeydown()) {
            continue;   // a release is a packet, but it is not a keystroke
        }

        ev.vkey = (VKey) getsysvar_vkeycode();
        ev.mods = (char) getsysvar_keymods();
        ev.ascii = (char) getsysvar_keyascii();

        return ev;
    }
}
