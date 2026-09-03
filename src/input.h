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

#ifndef _INPUT_H_
#define _INPUT_H_

#include "vkey.h"

// One keypress, as the VDP delivered it.
//
// Read from the key packet rather than with getch(). getch() waits for a byte
// in the typed-character buffer, and some combinations never put one there:
// CTRL+SHIFT with an arrow arrives as a packet carrying its character, its
// virtual key and its modifiers, but nothing lands in the buffer, so an editor
// waiting in getch() simply never wakes up for it. Measured on the device --
// holding CTRL+SHIFT and pressing RIGHT moves the packet counter, sets
// vkeycode to VK_RIGHT and keymods to CTRL|SHIFT, and getch() stays blocked
// until the modifiers are let go.
//
// Taking all three fields from the same packet fixes a second thing on the way
// past. They used to be read separately -- the character from getch(), then the
// virtual key and the modifiers from wherever the sysvars had got to -- so
// under load they could describe different keypresses.
typedef struct _key_event {
    char ascii;
    VKey vkey;
    char mods;
} key_event;

// Waits for a key to go down and returns it. Key releases are packets too, and
// are skipped: letting go of shift is not a keystroke.
key_event input_read(void);

#endif  // _INPUT_H_
