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

#ifndef _KEYS_H_
#define _KEYS_H_

#include "vkey.h"

// Where every keystroke in AED comes from.
//
// Not getch(). getch() returns typed *characters*, and a chord like
// CTRL+SHIFT+RIGHT does not produce one -- so a program blocked in it sleeps
// through the chord entirely, which is why word-wise selection never worked.
// MOS reports the chord perfectly well; it simply is not a character.
//
// So AED reads key *events* instead, through the queue in <agon/keyboard.h>.
// That fills a ring buffer from a MOS key vector, and each entry already
// carries the modifier state, which also removes a race: the old code called
// getch() and then read the keymods sysvar, by which time the modifier could
// already have been released.

typedef struct _key_press {
    // The ASCII the key produces, or 0 for the ones that produce none -- HOME
    // and PAGE UP among them. Anything deciding what a key means has to look
    // at vkey; ch is only good for the keys that put a character in the
    // document.
    char ch;
    VKey vkey;
    // MOD_CTRL, MOD_SHFT and MOD_ALT, as measured at the moment the key went
    // down. Left and right modifiers set the same bit: there is nothing here
    // to tell the sides apart, and nothing wants to.
    char mods;
} key_press;

// Installs the MOS key vector. Call once, after everything else that can fail,
// because keys_close is the only thing that takes it back.
void keys_open(void);

// Removes the vector. Not optional: MOS keeps calling it otherwise, into a
// buffer that no longer exists.
void keys_close(void);

// Blocks until a key goes down, and returns it. Releases are dropped -- nothing
// in AED acts on one -- as is the wait itself: this spins rather than sleeping,
// because the obvious way to yield, waitvblank(), hangs the editor outright.
key_press keys_wait(void);

#endif  // _KEYS_H_
