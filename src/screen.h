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

#ifndef _SCREEN_H_
#define _SCREEN_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _screen {
    char rows_;
    char cols_;
    char colors_;

    char currX_;
    char currY_;

    char topY_;
    char bottomY_;

    char tab_size_;
    char cursor_;
    char fg_;
    char bg_;
} screen;

// Tab width used when projecting a byte offset onto a screen column. Tabs are
// currently expanded to spaces before they reach the buffer, so nothing renders
// a tab yet; the width is threaded through so that stops being true.
#define SCR_DEFAULT_TAB_SIZE 4
#define SCR_MAX_TAB_SIZE     16

// Setup.
screen* scr_init(screen* scr, char cursor);
void scr_set_tab_size(screen* scr, char tab_size);
char scr_tab_size(screen* scr);
void scr_destroy(screen* scr);
void scr_clear(screen* scr);
void scr_footer(screen* scr, char* fname, bool dirty, int x, int y);

// Input.
void scr_putc(screen* scr, char ch, char* prefix, int psz, char* suffix, int ssz);
void scr_del(screen* scr, char* suffix, int sz);
void scr_bksp(screen* scr, char* suffix, int sz);

// Navigation.
void scr_left(screen* scr, char from_ch, char to_ch, int deltaX, char* suffix, int sz);
void scr_right(screen* scr, char from_ch, char to_c, int deltaX, char* prefix, int sz);
void scr_up(screen* scr, char from_ch, char to_ch, const char* line, int len);
void scr_down(screen* scr, char from_ch, char to_ch, const char* line, int len);

// Column projection. `line` is the current line from its start and `len` is how
// many of its bytes precede the cursor. scr_place_cursor is the single owner of
// the rule that currX_ is the clamped projection of the document column.
int  scr_column_of(screen* scr, const char* line, int len);
void scr_place_cursor(screen* scr, const char* line, int len);
void scr_home(screen* scr, char from_ch, char to_ch, char* prefix, int sz);
void scr_end(screen* scr, char from_ch, char to_ch, int deltaX, char* suffix, int sz);

// Screen management.
void set_colours(char fg, char bg);
void scr_clear_textarea(screen* scr, char top, char bottom);
void scr_write_line(screen* scr, char ypos, char* buf, int sz);
void scr_overwrite_line(screen* scr, char ypos, char* buf, int sz, int psz);

void scr_show_cursor_ch(screen* scr, char ch);
void scr_hide_cursor_ch(screen* scr, char ch);
void scr_erase(screen* scr, int sz);

#endif  // _SCREEN_H_
