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

#ifndef _USER_INPUT_H_
#define _USER_INPUT_H_

#include "char_buffer.h"
#include "config.h"
#include "keys.h"
#include "screen.h"

typedef enum _response {
    CANCEL_OPT = 0,
    YES_OPT = 1,
    NO_OPT = 2,
} RESPONSE;


typedef struct _user_input {
    char_buffer cb_;
    char ypos_;
    int cols_;
    // Where every prompt and modal here reads its keys: KEYS_MOS unless the
    // program says otherwise with ui_set_keys.
    const key_source* keys_;
} user_input;

user_input* ui_init(user_input* ui, int size, char ypos, int cols);
void ui_destroy(user_input* ui);

// Points the prompts and modals at another key source. The source is not
// copied, so it must outlive its use; NULL goes back to KEYS_MOS.
void ui_set_keys(user_input* ui, const key_source* ks);

// The prompt row and the width it has to fill, after the geometry moved. A
// font change alters both, and everything ui_ draws is placed from them -- a
// prompt left on the old bottom row lands in the middle of the document.
void ui_resize(user_input* ui, char ypos, int cols);

RESPONSE ui_goto(user_input* ui, screen* scr, int* line);
RESPONSE ui_color_picker(user_input* ui, screen* scr);
RESPONSE ui_dialog(user_input* ui, screen* scr, char* msg);

// States something and waits for a key. There is no question to answer, but it
// still has to block: the footer row is repainted at the top of every pass
// through the main loop, so a message that did not wait would be gone before it
// could be read.
void ui_message(user_input* ui, screen* scr, char* msg);

// What a key means to a list the reader is moving through, with the motion
// already applied to `at`. Both pickers had their own copy of this, one written
// as a chain of ifs and one as a switch, agreeing on every key.
typedef enum _pick_key {
    PICK_IDLE,          // nothing to do; redraw and wait again
    PICK_TAKE,          // RETURN: act on the row `at` names
    PICK_LEAVE          // ESC
} pick_key;

/*
 * The pieces a modal is built from, shared so that a program's menus and lists
 * are drawn the way AED's help and settings are: a title and a blank row at
 * the top, the program's rows, blank rows down to a prompt on the prompt row,
 * and the keys that move through a list. The ui_put_* helpers build a row's
 * text in a buffer `width` wide, returning where they stopped.
 *
 * A modal laid out on the whole screen -- the screen pointed at whole_, as
 * AED's help and settings do -- paints over every view on it, not only the
 * current one. A program showing more than one view has to repaint them all
 * when such a modal closes.
 */
void ui_modal_fill(screen* scr, char y, char bottom, const char* prompt,
                   int psz);
char ui_modal_title(screen* scr, char top, int width, const char* title);
pick_key ui_modal_move(VKey vkey, int* at, int max);
int ui_put_at(char* out, int at, int width, const char* s);
int ui_pad_to(char* out, int at, int width, int col);
int ui_put_num(char* out, int at, int width, int v);
RESPONSE ui_font_picker(user_input* ui, screen* scr, char* out, int max);

RESPONSE ui_text(
    user_input* ui,
    screen* scr,
    char* title,
    char* prefill,
    char** buf,
    int* sz);

#endif  // _USER_INPUT_H_
