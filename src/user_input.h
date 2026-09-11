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
} user_input;

user_input* ui_init(user_input* ui, int size, char ypos, int cols);
void ui_destroy(user_input* ui);

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
// Draws the command list over the text area and waits. Pages when the list is
// longer than the area, which it is on a 16-row font. Any key that is not a
// paging key closes it.
//
// It only draws: putting the document back is the caller's job, because the
// view cannot -- it has no access to the document. cmd_help does it the same
// way cmd_settings does.
void ui_help(user_input* ui, screen* scr);

// The startup banner, centred in the text area, for a session started with no
// file. Says what this is and where the commands are, and is wiped by the first
// keystroke rather than lingering behind the text.
void ui_banner(user_input* ui, screen* scr);

// The settings, editable. Draws over the text area; the caller puts the
// document back, as it does after the help. Returns YES_OPT when something was
// changed and is worth writing to the file.
//
// `cfg` comes in holding what AED is currently using and goes out holding the
// changes -- and only the changes: everything else stays unset, so writing it
// back with cfg_update cannot invent a setting the reader never asked for.
RESPONSE ui_settings(user_input* ui, screen* scr, config* cfg);

RESPONSE ui_text(
    user_input* ui,
    screen* scr,
    char* title,
    char* prefill,
    char** buf,
    int* sz);

#endif  // _USER_INPUT_H_
