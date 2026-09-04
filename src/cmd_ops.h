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

#ifndef _CMD_OPS_H_
#define _CMD_OPS_H_

#include "text_buffer.h"

#include <stdbool.h>
#include <stdint.h>

#include "vkey.h"

typedef struct _editor editor;

typedef struct _key {
    char key;
    VKey vkey;
} key;

typedef void(*cmd_op)(editor* ed);

void cmd_show(editor* ed);
bool cmd_quit(editor* ed);
bool cmd_save(editor* ed);
void cmd_save_as(editor* ed);
void cmd_open(editor* ed);

// Repaints screen rows fromY..toY from the document, highlighting whatever part
// of the selection falls on each. Knowing which bytes are on which row is
// document knowledge, so it lives here rather than in the view.
void cmd_repaint_rows(editor* ed, char fromY, char toY);

// Repaints one row, but only document columns [from_col, to_col). Used when a
// selection grows or shrinks within a row: the columns either side of the
// change already show what they should.
void cmd_repaint_span(editor* ed, char y, int from_col, int to_col);

// The selection, as two positions in document order. Only meaningful while one
// is being made.
void cmd_selection_range(editor* ed, tb_pos* from, tb_pos* to);

// Deletes the selected text and leaves the cursor where it began. Used when a
// key that changes the document arrives with a selection live: the selection is
// what it replaces.
bool cmd_delete_selection(editor* ed);

void cmd_copy(editor* ed);
void cmd_cut(editor* ed);
void cmd_paste(editor* ed);
void cmd_select_all(editor* ed);
void cmd_color_picker(editor* ed);

void cmd_putc(editor* ed, key k);
void cmd_del(editor* ed);
void cmd_bksp(editor* ed);
void cmd_newl(editor* ed);
void cmd_del_line(editor* ed);
void cmd_left(editor* ed);
void cmd_w_left(editor* ed);
void cmd_w_right(editor* ed);
void cmd_right(editor* ed);
void cmd_up(editor* ed);
void cmd_down(editor* ed);
void cmd_home(editor* ed);
void cmd_end(editor* ed);
void cmd_page_up(editor* ed);
void cmd_page_down(editor* ed);
void cmd_goto(editor* ed);

#endif  // _CMD_OPS_H_
