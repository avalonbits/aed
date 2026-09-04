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

#ifndef _EDITOR_H_
#define _EDITOR_H_

#include <stdint.h>

#include "clipboard.h"
#include "cmd_ops.h"
#include "vkey.h"
#include "screen.h"
#include "text_buffer.h"
#include "user_input.h"

typedef struct _editor {
    screen scr_;
    text_buffer buf_;
    user_input ui_;

    // Where the selection started. Held here rather than in the buffer because
    // it is about intent, not about the document: the model has no idea any of
    // this is happening, and a selection means nothing once the file changes.
    tb_pos anchor_;
    bool selecting_;

    clipboard clip_;
} editor;

editor* ed_init(editor* ed, int mem_kb, const char* fname);

// True for the keys that move the cursor without changing the document. Holding
// shift with one of these is what starts and extends a selection; anything else
// ends it.
bool ed_is_motion(VKey vkey);

// True for the modifier keys themselves. MOS reports the press of one as an
// event in its own right, before the key it modifies, so anything deciding what
// a keypress means has to skip them -- otherwise holding shift is itself a
// keystroke, and it arrives right in the middle of the selection it is making.
bool ed_is_modifier(VKey vkey);

void ed_destroy(editor* ed);

void ed_run(editor* ed);

// Three commands the main loop handles itself rather than calling through, so
// they are sentinels in the table rather than functions.
#define CMD_PUTC    (cmd_op) 0x01
#define CMD_QUIT    (cmd_op) 0x02
#define CMD_SAVE    (cmd_op) 0x03

typedef struct _key_command {
    cmd_op cmd;
    key k;
    // Carried out of read_input because whether a motion extends a selection is
    // decided above the command, not inside it: cmd_left does the same thing
    // either way.
    char mods;
} key_command;

// Blocks for the next key and works out what it means. Declared here so the
// translation can be tested directly: it is where a chord that MOS reports
// perfectly well can still be lost, and nothing below it would notice.
key_command read_input(void);

// What a key means with CTRL held. Declared here so the bindings can be
// asserted directly: a command that exists but is not reachable from the
// keyboard is not a feature, and nothing below this level would notice.
key_command ctrlCmds(key_command kc, char mods);

typedef enum _sel_action {
    SEL_NONE = 0,   // there was no selection and there still is none
    SEL_EXTEND,     // one was started or is being extended
    SEL_DROP,       // one was in progress and this key ended it
    SEL_REPLACE,    // ...and the key changes the document, so it takes its place
} sel_action;

// True for the keys that put something in the document or take something out.
// With a selection live these replace it rather than acting alongside it, so
// they have to be told apart from the ones that merely end it.
bool ed_key_edits(key_command kc);

// Applies a keypress to the selection, before the command it names runs. The
// return value tells the caller whether anything needs repainting, which is why
// this is separate from running the command: the decision is made from the key
// and the answer is needed again afterwards.
sel_action ed_selection_for(editor* ed, key_command kc);

// Repaints whatever the selection changed, after the command has run. The
// "before" values are read before it: the cursor's screen row and column, the
// document line then at the top of the screen, and the horizontal scroll
// origin. Between them they say whether the view moved under the text, which
// decides how much has to be redrawn -- and when it did not move, x_before says
// which columns of the row changed, so only those need sending.
void ed_selection_repaint(editor* ed, sel_action act, char y_before,
                          char x_before, int top_before, int origin_before);

#endif  // _EDITOR_H_
