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
    // Document column shown at screen column 0. The view scrolls horizontally
    // by moving this rather than by slicing lines at a byte offset, which is
    // what lets one byte occupy more than one column.
    int originX_;
    char cursor_;
    char fg_;
    char bg_;
    // What the Agon was using before AED started. The editor's own scheme is
    // its business; the colours the user left the machine in are theirs, so
    // they are put back on exit whatever AED was showing.
    char entryFg_;
    char entryBg_;
    // Columns of the row being painted that are inside the selection, as
    // [selFrom_, selTo_). Set for one row at a time by scr_write_line_sel and
    // cleared again by it, so no other painter can inherit a highlight meant
    // for a different line. selOn_ is whether the colours are currently
    // swapped, so a highlighted run costs two colour changes and not one per
    // character.
    int selFrom_;
    int selTo_;
    char selOn_;
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

// The colour scheme AED starts with is whatever the Agon was already using --
// scr_init measures it. These let it be read back for the settings file and set
// from one, without reaching into the struct.
void scr_set_scheme(screen* scr, char fg, char bg);
char scr_fg(screen* scr);
char scr_bg(screen* scr);
void scr_destroy(screen* scr);
void scr_clear(screen* scr);
void scr_footer(screen* scr, char* fname, bool dirty, int x, int y);

// Input.
// These return how far the horizontal origin moved (see scr_place_cursor);
// non-zero means every other row is drawn against the old origin.
int scr_putc(screen* scr, char ch, char* prefix, int psz, char* suffix, int ssz);
void scr_del(screen* scr, char* suffix, int sz);
int scr_bksp(screen* scr, char* prefix, int psz, char* suffix, int ssz);

// Navigation.
// These no longer paint the row -- when the window moves the controller repaints
// the whole text area -- so they only need where the cursor sits in the line.
int scr_up(screen* scr, char from_ch, char to_ch, const char* pre, int presz);
int scr_down(screen* scr, char from_ch, char to_ch, const char* pre, int presz);

// Column projection. `line` is the current line from its start and `len` is how
// many of its bytes precede the cursor.
int  scr_column_of(screen* scr, const char* line, int len);

// Inverse projection: the byte offset in `line` that renders at or after
// `column`. Clamped to `len`.
int  scr_byte_at(screen* scr, const char* line, int len, int column);

// Places the cursor at the column `len` bytes into the line, scrolling the view
// horizontally if that column is off screen. Returns how far the origin moved,
// in columns: positive means the window moved right, negative left, zero not at
// all. The caller needs the distance, not just the fact, to decide between a
// VDP region scroll and a full repaint.
int scr_place_cursor(screen* scr, const char* line, int len);

// Beyond this many columns a full repaint costs less than scrolling and
// painting the newly exposed columns one at a time.
#define SCR_MAX_HSCROLL 16

// Scrolls the whole text area sideways using the VDP's own region scroll, the
// same mechanism as scr_scroll_up/down. Positive `cols` moves the window right
// (text moves left). The exposed columns are left blank for the caller to fill.
void scr_scroll_h(screen* scr, int cols);

// The glyph rendered at document column `col` of `line`, or a space when that
// column falls past the end or inside a tab's expansion.
char scr_glyph_at(screen* scr, const char* line, int len, int col);

// Prints one character at a screen cell without disturbing the recorded cursor.
void scr_put_at(screen* scr, char sx, char sy, char ch);

// Paints one row from a line held as a gap-buffer split (either half may be
// NULL/0), starting at the current horizontal origin, expanding tabs and
// padding to the full width.
void scr_paint_row(screen* scr, char ypos, const char* pre, int presz,
                   const char* suf, int sufsz);

// As above, but starting at an arbitrary document column -- used after an
// insertion, which must repaint from the inserted character, not the cursor.
void scr_paint_from(screen* scr, char ypos, const char* pre, int presz,
                    const char* suf, int sufsz, int from_col);

// Repaints from the cursor's column to the right edge. Used after an edit,
// where nothing to the left of the cursor can have changed.
void scr_paint_tail(screen* scr, const char* suf, int sufsz);

// The single horizontal-motion primitive: move the cursor to the given position
// in the line, scrolling and repainting the row if required.
int scr_move_cursor(screen* scr, char from_ch, char to_ch,
                    const char* pre, int presz);

// Screen management.
void set_colours(char fg, char bg);

// Scrolls one row of the text area and paints the newly exposed line. The
// caller decides which region moves and what belongs on the new row; the VDU
// sequences that make it happen live in the view.
void scr_scroll_up(screen* scr, char topY, char bottomY, char* line, int sz, char ch);
void scr_scroll_down(screen* scr, char topY, char bottomY, char* line, int sz, char ch);

// Moves the hardware cursor to the position the screen already records.
void scr_sync_cursor(screen* scr);
void scr_clear_textarea(screen* scr, char top, char bottom);
void scr_write_line(screen* scr, char ypos, char* buf, int sz);

// As scr_write_line, with the columns in [from_col, to_col) drawn in the
// reversed scheme. Columns are screen columns, so the caller has already
// resolved tabs -- scr_column_of turns a byte offset into one. An empty or
// backwards span paints the row plainly.
void scr_write_line_sel(screen* scr, char ypos, char* buf, int sz,
                        int from_col, int to_col);
void scr_overwrite_line(screen* scr, char ypos, char* buf, int sz, int psz);

void scr_show_cursor_ch(screen* scr, char ch);
void scr_hide_cursor_ch(screen* scr, char ch);
void scr_erase(screen* scr, int sz);

#endif  // _SCREEN_H_
