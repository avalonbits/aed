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

#include "screen.h"

#include <agon/vdp.h>
#include <string.h>
#include <agon/mos.h>
#include <stdio.h>

#include "conv.h"

#define MAX_COLS 255

// Characters on their way to the VDP, held back so they go in one call.
//
// putchar is `rst.lil $10` -- one entry into MOS per byte -- so painting a row
// a character at a time is eighty of them, and the editor does that on a
// keystroke. Everything that draws runs of text writes here instead, and the
// run is sent when the colour changes or the row ends.
static char out_buf[MAX_COLS];
static int out_len;

static void out_flush(void) {
    if (out_len > 0) {
        mos_puts(out_buf, out_len, 0);
        out_len = 0;
    }
}

static void out_ch(char ch) {
    if (out_len >= (int) sizeof(out_buf)) {
        out_flush();
    }
    out_buf[out_len++] = ch;
}

static void out_str(const char* str, int n) {
    for (int i = 0; i < n; i++) {
        out_ch(str[i]);
    }
}

static void out_run(char ch, int n) {
    for (int i = 0; i < n; i++) {
        out_ch(ch);
    }
}

void set_colours(char fg, char bg) {
    // Anything buffered belongs to the colours that are still current.
    out_flush();

    // Both colours in one write. VDU 17 takes one parameter, so this is two
    // commands, but they are four bytes and there is no reason to enter MOS
    // twice for them -- and the editor changes colours several times a
    // keystroke, around the cursor cell and at each end of a highlight.
    char vdu[4];
    vdu[0] = 17;
    vdu[1] = fg;
    vdu[2] = 17;
    vdu[3] = (char) (bg + 128);
    mos_puts(vdu, sizeof(vdu), 0);
}

// The cursor cell shows the character under it, but a control byte cannot be
// drawn -- sending a tab to the VDP moves the cursor instead of painting it,
// which left the cursor invisible whenever it sat on one.
static char cursor_glyph(screen* scr, char ch) {
    if (ch == 0 || ch == '\r' || ch == '\n' || ch == '\t') {
        return scr->cursor_;
    }

    return ch;
}

// Drawing the cursor is four things -- reverse the colours, put the character
// down, step back over it, put the colours back -- and ten bytes. It happens on
// every keystroke, so it goes in one write rather than four.
void scr_show_cursor_ch(screen* scr, char ch) {
    ch = cursor_glyph(scr, ch);
    out_flush();

    char vdu[10];
    vdu[0] = 17;
    vdu[1] = scr->bg_;
    vdu[2] = 17;
    vdu[3] = (char) (scr->fg_ + 128);
    vdu[4] = ch;
    vdu[5] = 8;                         // VDU 8: back over the cell just drawn
    vdu[6] = 17;
    vdu[7] = scr->fg_;
    vdu[8] = 17;
    vdu[9] = (char) (scr->bg_ + 128);
    mos_puts(vdu, sizeof(vdu), 0);
}

static void scr_show_cursor(screen* scr) {
    scr_show_cursor_ch(scr, scr->cursor_);
}

static void vdp_puts(char* str, char sz) {
    volatile uint8_t* sysvar = mos_sysvars();
    sysvar[sysvar_vdp_pflags] = 0;
    mos_puts(str, sz, 0);

    for (;;) {
        waitvblank();
        sysvar = mos_sysvars();
        if ((sysvar[sysvar_vdp_pflags] & 0x04) != 0) {
            break;
        }
    }
}

static char getColorForCh(char ch) {
    static char getcol[7] = {23, 0, 0x84, 4, 0, 4, 0};

    vdp_cursor_tab(0,0);
    putchar(ch);

    volatile char idx = 0;
    for (int i = 0; i < 1; i++) {
        waitvblank();
        volatile char* sysvar = (volatile char*) mos_sysvars();
        idx = sysvar[sysvar_scrpixelIndex];
    }

    vdp_puts(getcol, sizeof(getcol));
    for (int i = 0; i < 1; i++) {
        waitvblank();
        volatile char* sysvar = (volatile char*) mos_sysvars();
        idx = sysvar[sysvar_scrpixelIndex];
    }

    return idx;
}

static void get_active_colours(screen* scr) {
    static char logic[4] = {23, 0, 0xC0, 0};
    VDP_PUTS(logic);

    scr->fg_ = getColorForCh('*');
    scr->bg_ = getColorForCh(' ');
    scr->entryFg_ = scr->fg_;
    scr->entryBg_ = scr->bg_;
    set_colours(scr->fg_, scr->bg_);
}

screen *scr_init(screen* scr, char cursor) {
    // VDU 23,16,setting,mask -- new = (current AND mask) EOR setting. With
    // mask 0 this sets the whole byte to 1: bit 0, scroll protection. It has
    // never had anything to do with cursor wrap, which is bit 4.
    static char enable_scroll_protect[4] = {23, 16, 1, 0};
    VDP_PUTS(enable_scroll_protect);

    vdp_cursor_enable(false);
    scr->rows_ = getsysvar_scrRows();

    // One column narrower than the screen reports. The rightmost column is
    // never written, never scrolled, and never holds the cursor.
    //
    // Writing the last column of a row stops the next keystroke arriving.
    // Measured on hardware with a probe that does nothing else: eleven
    // characters written at column 0 are harmless, and the same eleven written
    // against the right edge stop the following key until something breaks the
    // sequence. Not the byte count, not the number of writes, not the colours
    // around it, not moving the cursor back afterwards -- each ruled out by a
    // probe differing from a working one in one line.
    //
    // The cause is in the VDP, in Context::plotString (video/context/graphics.h):
    //
    //     if (!cursorBehaviour.xHold) {
    //         cursorRight();
    //         if (cursorIsOffRight()) {
    //             checkPagedMode();
    //         }
    //     }
    //
    // and checkPagedMode, with CTRL and SHIFT both down, sets the processor to
    // CtrlShiftPaused -- the BBC "pause output" chord. This editor's selection
    // chord is that chord. Writing the last column is what moves the cursor off
    // the right edge, so it is what arms the pause; a row that stops one column
    // short never makes the call.
    //
    // The pending newline that scroll protection leaves is *not* the cause: it
    // is consumed by cursorAutoNewline -> cursorCR + cursorDown, and none of
    // those reach checkPagedMode. That is why every probe that tried to cancel
    // it with VDU 8 failed -- there was nothing to cancel. The pause is
    // synchronous with the off-right transition, not deferred to the wrap.
    //
    // It cannot be switched off. Paged mode does not gate it, VDP variable
    // 0x1022 covers only the CTRL-alone branch, kbEnabled is never cleared,
    // !textCursorActive() means VDU 5, and scrollProtect guards only the
    // post-string newline. cursorBehaviour.xHold (bit 5) would skip the block
    // outright, but a VDP that does not implement it wraps and, on the bottom
    // row, scrolls the screen -- and MOS offers no way to ask the VDP its
    // version. Not writing the column at all is what works on every VDP.
    //
    // The cost is one column of width. It is a deliberate right margin rather
    // than a lost column, and it is why cols_ is the usable width everywhere
    // else in this file.
    scr->cols_ = getsysvar_scrCols() - 1;
    scr->colors_ = getsysvar_scrColours();
    scr->cursor_ = cursor;
    scr->lastFname_[0] = 0;
    scr->lastPosW_ = 0;
    scr->footerDrawn_ = false;
    scr->topY_ = 1;
    scr->originX_ = 0;
    scr->selFrom_ = 0;
    scr->selTo_ = 0;
    scr->selOn_ = 0;
    scr->bottomY_ = scr->rows_-1;
    get_active_colours(scr);
    scr_clear(scr);
    scr_show_cursor(scr);
    vdp_cursor_home();
    scr->tab_size_ = SCR_DEFAULT_TAB_SIZE;

    return scr;
}

void scr_set_ctrl_pause_frames(screen* scr, int frames) {
    (void) scr;
    if (frames < 0 || frames > 255) {
        return;
    }

    // VDU 23, 0, &F8, flag; value; -- flag and value are 16-bit, low byte
    // first. The VDU variable block starts at 0x1000 and the frame count is
    // 0x22 within it.
    char vdu[7];
    vdu[0] = 23;
    vdu[1] = 0;
    vdu[2] = (char) 0xF8;
    vdu[3] = 0x22;
    vdu[4] = 0x10;
    vdu[5] = (char) (frames & 0xFF);
    vdu[6] = 0;
    mos_puts(vdu, sizeof(vdu), 0);
}

void scr_set_tab_size(screen* scr, char tab_size) {
    if (tab_size < 1) {
        tab_size = 1;
    } else if (tab_size > SCR_MAX_TAB_SIZE) {
        tab_size = SCR_MAX_TAB_SIZE;
    }
    scr->tab_size_ = tab_size;
}

char scr_tab_size(screen* scr) {
    return scr->tab_size_;
}

void scr_set_scheme(screen* scr, char fg, char bg) {
    if (fg < 0 || bg < 0 || fg >= scr->colors_ || bg >= scr->colors_) {
        return;   // outside what this screen mode can show
    }
    scr->fg_ = fg;
    scr->bg_ = bg;
    set_colours(scr->fg_, scr->bg_);
}

char scr_fg(screen* scr) {
    return scr->fg_;
}

char scr_bg(screen* scr) {
    return scr->bg_;
}

// Maps a byte offset within a line onto the screen column it renders at. A tab
// advances to the next multiple of the tab width; every other byte is one
// column wide. With no tabs in the line this is simply `len`, which is why this
// is behaviour-preserving today.
int scr_column_of(screen* scr, const char* line, int len) {
    if (line == NULL || len <= 0) {
        return 0;
    }

    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;
    int col = 0;
    for (int i = 0; i < len; i++) {
        if (line[i] == '\t') {
            col += tab - (col % tab);
        } else {
            col++;
        }
    }

    return col;
}

int scr_byte_at(screen* scr, const char* line, int len, int column) {
    if (line == NULL || len <= 0 || column <= 0) {
        return 0;
    }

    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;
    int col = 0;
    for (int i = 0; i < len; i++) {
        if (col >= column) {
            return i;
        }
        if (line[i] == '\t') {
            col += tab - (col % tab);
        } else {
            col++;
        }
    }

    return len;
}

int scr_place_cursor(screen* scr, const char* line, int len) {
    const int col = scr_column_of(scr, line, len);
    const int width = scr->cols_ > 0 ? scr->cols_ : 1;
    const int origin = scr->originX_;

    // Keep the cursor inside the window by moving the window, not by pinning
    // the cursor to an edge and losing track of where it really is.
    if (col < origin) {
        // Scrolling left: if everything up to the cursor already fits, show the
        // line from its start rather than leaving the window parked mid-line,
        // which would blank out every shorter row on screen.
        scr->originX_ = col < width ? 0 : col;
    } else if (col > origin + width - 1) {
        scr->originX_ = col - (width - 1);
    }

    int x = col - scr->originX_;
    if (x > width - 1) {
        x = width - 1;
    }
    if (x < 0) {
        x = 0;
    }
    scr->currX_ = (char) x;

    return scr->originX_ - origin;
}

void scr_destroy(screen* scr) {
    // mask 1 keeps only the current bit 0 and the EOR flips it, so this clears
    // scroll protection and leaves the rest of the byte zeroed.
    static char disable_scroll_protect[4] = {23, 16, 1, 1};
    VDP_PUTS(disable_scroll_protect);
    vdp_cursor_enable(true);

    // Hand the machine back as it was found: restore the colours first, then
    // clear, so the cleared screen is in the user's background and not AED's.
    set_colours(scr->entryFg_, scr->entryBg_);
    vdp_clear_screen();

    scr->currX_ = 0;
    scr->currY_ = 0;
    scr->rows_ = 0;
    scr->cols_ = 0;
}

void scr_footer_invalidate(screen* scr) {
    scr->footerDrawn_ = false;
}

// How wide the position field is for these numbers: four columns for the line
// and six for the column, or more when a number does not fit. A document can
// outgrow four digits of line number, and the row still has to total cols_.
static int position_width(int x, int y) {
    static char digits[16];

    i2s(y, digits, 16);
    int w = strlen(digits);
    if (w < 4) {
        w = 4;
    }
    i2s(x, digits, 16);
    int xw = strlen(digits);
    if (xw < 6) {
        xw = 6;
    }

    return w + 1 + xw;
}

// Writes just "  12,34    " at the cursor. Padding is to the field width, and
// a number that fills the field gets none -- the old code added a full field's
// worth of spaces instead of none once the line number reached four digits,
// which pushed the column out of the footer and off the row.
static void footer_position(int x, int y) {
    static char digits[16];

    i2s(y, digits, 16);
    int dsz = strlen(digits);
    out_run(' ', 4 - dsz);
    out_str(digits, dsz);
    out_ch(',');

    i2s(x, digits, 16);
    dsz = strlen(digits);
    out_str(digits, dsz);
    out_run(' ', 6 - dsz);
}

void scr_footer(screen* scr, char* fname, bool dirty, int x, int y) {
    static char* no_file = "[NO FILE]";
    if (fname == NULL) {
        fname = no_file;
    }
    const int fnsz = strlen(fname);

    // Nothing has changed, so there is nothing to send. The caller repaints the
    // footer on every pass of the event loop; on all but a handful of those the
    // three things it shows are the same as last time.
    const int posw = position_width(x, y);
    const bool same_file = scr->footerDrawn_
        && scr->lastDirty_ == dirty
        && scr->lastPosW_ == posw
        && strcmp(scr->lastFname_, fname) == 0;
    if (same_file && scr->lastX_ == x && scr->lastY_ == y) {
        return;
    }
    // Remember it before drawing, not after: a name too long for the cache is
    // truncated the same way on every pass, so it still compares equal and the
    // repaint still stops.
    strncpy(scr->lastFname_, fname, sizeof(scr->lastFname_) - 1);
    scr->lastFname_[sizeof(scr->lastFname_) - 1] = 0;
    scr->lastDirty_ = dirty;
    scr->lastX_ = x;
    scr->lastY_ = y;
    scr->lastPosW_ = posw;
    scr->footerDrawn_ = true;

    // The common case by far: the cursor moved and nothing else did. Only the
    // position field can differ, so only it is sent. Redrawing the whole row
    // for this costs a MOS call per column -- the padding is a putchar loop --
    // and the row is mostly spaces that were already spaces.
    if (same_file) {
        vdp_cursor_tab((char) (scr->cols_ - posw), scr->bottomY_);
        set_colours(scr->bg_, scr->fg_);
        footer_position(x, y);
        set_colours(scr->fg_, scr->bg_);
        vdp_cursor_tab(scr->currX_, scr->currY_);

        return;
    }

    vdp_cursor_tab(0, scr->bottomY_);
    set_colours(scr->bg_, scr->fg_);

    out_str(fname, fnsz);
    out_ch(dirty ? '*' : ' ');
    out_ch(' ');
    out_run(' ', scr->cols_ - fnsz - 2 - posw);
    footer_position(x, y);

    set_colours(scr->fg_, scr->bg_);
    vdp_cursor_tab(scr->currX_, scr->currY_ );
}

char* title = "AED: Another Text Editor";
void scr_clear(screen* scr) {
    // The footer goes with everything else, so it has to be drawn again.
    scr->footerDrawn_ = false;
    vdp_clear_screen();
    vdp_cursor_home();
    vdp_cursor_tab(0,0);
    const int len = strlen(title);
    const int banner = (scr->cols_ - len)/2;
    out_run('-', banner);
    set_colours(scr->bg_, scr->fg_);
    out_str(title, strlen(title));
    set_colours(scr->fg_, scr->bg_);
    out_run('-', banner);
    out_flush();
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    scr->originX_ = 0;
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

void scr_hide_cursor_ch(screen* scr, char ch) {
    ch = cursor_glyph(scr, ch);
    out_flush();

    char vdu[6];
    vdu[0] = 17;
    vdu[1] = scr->fg_;
    vdu[2] = 17;
    vdu[3] = (char) (scr->bg_ + 128);
    vdu[4] = ch;
    vdu[5] = 8;
    mos_puts(vdu, sizeof(vdu), 0);
}

static void scr_hide_cursor(screen* scr) {
    scr_hide_cursor_ch(scr, scr->cursor_);
}

// `prefix`/`psz` describe the line up to and including the character just
// inserted, so the cursor lands after it.
int scr_putc(screen* scr, char ch, char* prefix, int psz, char* suffix, int ssz) {
    (void) ch;
    scr_hide_cursor(scr);

    // Repaint from where the inserted character starts, not from the cursor:
    // the cursor now sits after it, and a tab starts several columns back.
    const int at = scr_column_of(scr, prefix, psz > 0 ? psz - 1 : 0);
    const int scrolled = scr_place_cursor(scr, prefix, psz);
    if (scrolled == 0) {
        scr_paint_from(scr, scr->currY_, prefix, psz, suffix, ssz, at);
        scr_sync_cursor(scr);
        scr_show_cursor_ch(scr,
                           (suffix != NULL && ssz > 0) ? suffix[0] : scr->cursor_);
    }

    return scrolled;
}

void scr_del(screen* scr, char* suffix, int sz) {
    scr_paint_tail(scr, suffix, sz);
    scr_show_cursor_ch(scr, sz > 0 ? suffix[0] : scr->cursor_);
}

// `prefix`/`psz` describe the line after the deletion, so the cursor lands on
// the character that moved into the deleted position.
int scr_bksp(screen* scr, char* prefix, int psz, char* suffix, int ssz) {
    scr_hide_cursor(scr);
    const int scrolled = scr_place_cursor(scr, prefix, psz);
    if (scrolled == 0) {
        scr_paint_tail(scr, suffix, ssz);
        scr_sync_cursor(scr);
        scr_show_cursor_ch(scr, ssz > 0 ? suffix[0] : scr->cursor_);
    }

    return scrolled;
}





int scr_up(screen* scr, char from_ch, char to_ch,
           const char* pre, int presz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_--;
    const int scrolled = scr_place_cursor(scr, pre, presz);
    if (scrolled == 0) {
        vdp_cursor_tab(scr->currX_, scr->currY_);
        scr_show_cursor_ch(scr, to_ch);
    }

    return scrolled;
}

int scr_down(screen* scr, char from_ch, char to_ch,
           const char* pre, int presz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_++;
    const int scrolled = scr_place_cursor(scr, pre, presz);
    if (scrolled == 0) {
        vdp_cursor_tab(scr->currX_, scr->currY_);
        scr_show_cursor_ch(scr, to_ch);
    }

    return scrolled;
}

// VDU 28, left, bottom, right, top -- define a text viewport.
// VDU 28 takes character *positions*, so `right` is the index of the last
// column, not the number of columns. Every call here passed cols_, which is one
// past the end of the screen -- an out-of-range viewport, and the VDP is not
// documented to say what it does with one. What it did: the horizontal scroll
// stopped working at the right edge, and writes afterwards went down a column
// instead of along the row.
static void define_viewport(char left, char bottom, char right, char top) {
    static char viewport[5] = {28, 0, 0, 0, 0};
    viewport[1] = left;
    viewport[2] = bottom;
    viewport[3] = right;
    viewport[4] = top;
    VDP_PUTS(viewport);
}

// VDU 26 -- restore the default viewport.
static void reset_viewport(void) {
    putchar(26);
}

void scr_clear_textarea(screen* scr, char top, char bottom) {
    // The viewport includes `bottom`, and the callers that refresh the whole
    // screen pass bottomY_ -- which is the footer row. So this erases the
    // footer even though nothing here draws it back. That went unnoticed while
    // the footer was redrawn on every pass of the event loop; now that an
    // unchanged one sends nothing, it has to be said out loud or a full
    // refresh leaves the row blank until the cursor happens to move.
    if (bottom >= scr->bottomY_) {
        scr->footerDrawn_ = false;
    }
    define_viewport(0, bottom, (char) (scr->cols_ - 1), top);
    vdp_clear_screen();
    reset_viewport();
}

// Emits one line's worth of cells starting at document column `from_col`,
// expanding tabs, stopping after `budget` screen columns. Returns the document
// column reached, so a caller can continue across the gap split.
// Swaps the colours on the way into the selection and back on the way out, so
// a highlighted run costs two colour changes rather than one per character --
// which matters on a VDP behind a serial link.
static void highlight(screen* scr, int col) {
    const char want = (col >= scr->selFrom_ && col < scr->selTo_) ? 1 : 0;
    if (want == scr->selOn_) {
        return;
    }
    if (want) {
        set_colours(scr->bg_, scr->fg_);
    } else {
        set_colours(scr->fg_, scr->bg_);
    }
    scr->selOn_ = want;
}

static int emit_span(screen* scr, const char* buf, int sz, int col,
                     int from_col, int stop_col) {
    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;

    for (int i = 0; i < sz && col < stop_col; i++) {
        int width = 1;
        if (buf[i] == '\t') {
            width = tab - (col % tab);
        }
        for (int w = 0; w < width && col < stop_col; w++, col++) {
            if (col >= from_col) {
                highlight(scr, col);
                out_ch(buf[i] == '\t' ? ' ' : buf[i]);
            }
        }
        continue;
    }

    return col;
}

// Paints the row from document column `from_col` rightwards. Columns left of
// the window, or left of from_col, are skipped rather than redrawn.
void scr_paint_span(screen* scr, char ypos, const char* pre, int presz,
                    const char* suf, int sufsz, int from_col, int to_col) {
    const int edge = scr->originX_ + scr->cols_;
    const int from = from_col > scr->originX_ ? from_col : scr->originX_;
    const int stop = to_col < edge ? to_col : edge;
    if (from >= stop) {
        return;
    }

    vdp_cursor_tab((char)(from - scr->originX_), ypos);
    scr->selOn_ = 0;
    int col = 0;
    if (pre != NULL && presz > 0) {
        col = emit_span(scr, pre, presz, col, from, stop);
    }
    if (suf != NULL && sufsz > 0) {
        col = emit_span(scr, suf, sufsz, col, from, stop);
    }
    // The padding past the end of the text is highlighted too when the
    // selection runs through the line break, which is how a selected newline
    // shows up as anything at all.
    for (; col < stop; col++) {
        if (col >= from) {
            highlight(scr, col);
            out_ch(' ');
        }
    }
    out_flush();
    if (scr->selOn_) {
        set_colours(scr->fg_, scr->bg_);
        scr->selOn_ = 0;
    }
    scr_sync_cursor(scr);
}

void scr_paint_from(screen* scr, char ypos, const char* pre, int presz,
                    const char* suf, int sufsz, int from_col) {
    scr_paint_span(scr, ypos, pre, presz, suf, sufsz, from_col,
                   scr->originX_ + scr->cols_);
}

void scr_write_line_sel(screen* scr, char ypos, char* buf, int sz,
                        int from_col, int to_col) {
    scr_write_line_span(scr, ypos, buf, sz, from_col, to_col,
                        scr->originX_, scr->originX_ + scr->cols_);
}

void scr_write_line_span(screen* scr, char ypos, char* buf, int sz,
                         int from_col, int to_col, int paint_from,
                         int paint_to) {
    scr->selFrom_ = from_col;
    scr->selTo_ = to_col;
    scr_paint_span(scr, ypos, NULL, 0, buf, sz, paint_from, paint_to);
    scr->selFrom_ = 0;
    scr->selTo_ = 0;
}

void scr_paint_row(screen* scr, char ypos, const char* pre, int presz,
                   const char* suf, int sufsz) {
    scr_paint_from(scr, ypos, pre, presz, suf, sufsz, scr->originX_);
}

void scr_paint_tail(screen* scr, const char* suf, int sufsz) {
    const int at = scr->originX_ + scr->currX_;
    const int stop = scr->originX_ + scr->cols_;

    vdp_cursor_tab(scr->currX_, scr->currY_);
    int col = at;
    if (suf != NULL && sufsz > 0) {
        col = emit_span(scr, suf, sufsz, col, at, stop);
    }
    out_run(' ', stop - col);
    scr_sync_cursor(scr);
}

// Returns true when the horizontal origin moved. The origin is screen-wide, so
// every other visible row is then drawn against the old one and the caller must
// repaint the text area -- the view cannot, it has no access to the document.
int scr_move_cursor(screen* scr, char from_ch, char to_ch,
                    const char* pre, int presz) {
    scr_hide_cursor_ch(scr, from_ch);
    const int scrolled = scr_place_cursor(scr, pre, presz);
    if (scrolled == 0) {
        scr_sync_cursor(scr);
        scr_show_cursor_ch(scr, to_ch);
    }

    return scrolled;
}

void scr_write_line(screen* scr, char ypos, char* buf, int sz) {
    scr_paint_row(scr, ypos, NULL, 0, buf, sz);
}

void scr_overwrite_line(screen* scr, char ypos, char* buf, int sz, int psz) {
    (void) psz;   // scr_paint_row always pads to the full width
    scr_paint_row(scr, ypos, NULL, 0, buf, sz);
}

void scr_sync_cursor(screen* scr) {
    out_flush();
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

// VDU 23,7,extent,direction,movement -- scroll the current text viewport by one
// character row. Direction 2 is down, 3 is up.
static void scroll_region(
        screen* scr, char topY, char bottomY, const char* vdu, char sz,
        char* line, int lsz, char ch) {
    define_viewport(0, bottomY, (char) (scr->cols_ - 1), topY);
    mos_puts((char*) vdu, sz, 0);
    reset_viewport();
    scr_paint_row(scr, scr->currY_, NULL, 0, line, lsz);
    scr_sync_cursor(scr);
    scr_show_cursor_ch(scr, ch);
}

// VDU 23,7,extent,direction,movement -- direction 0 moves the content right,
// 1 moves it left. Scrolling the window right means moving the content left.
void scr_scroll_h(screen* scr, int cols) {
    if (cols == 0) {
        return;
    }

    static char scroll[5] = {23, 7, 0, 0, 0};
    int n = cols;
    if (n < 0) {
        n = -n;
        scroll[3] = 0;   // window left  -> content right
    } else {
        scroll[3] = 1;   // window right -> content left
    }
    scroll[4] = 8;       // one character cell

    define_viewport(0, scr->bottomY_ - 1, (char) (scr->cols_ - 1), scr->topY_);
    for (int i = 0; i < n; i++) {
        VDP_PUTS(scroll);
    }
    reset_viewport();
}

char scr_glyph_at(screen* scr, const char* line, int len, int col) {
    if (line == NULL || len <= 0 || col < 0) {
        return ' ';
    }

    const int tab = scr->tab_size_ > 0 ? scr->tab_size_ : 1;
    int at = 0;
    for (int i = 0; i < len; i++) {
        const int width = line[i] == '\t' ? tab - (at % tab) : 1;
        if (col < at + width) {
            // Inside a tab's expansion, or past the start of a normal cell.
            return (line[i] == '\t' || col > at) ? ' ' : line[i];
        }
        at += width;
    }

    return ' ';
}

void scr_put_at(screen* scr, char sx, char sy, char ch) {
    vdp_cursor_tab(sx, sy);
    putchar(ch);
    scr_sync_cursor(scr);
}

void scr_scroll_down(
        screen* scr, char topY, char bottomY, char* line, int sz, char ch) {
    static const char down[] = {23, 7, 0, 2, 8};
    scroll_region(scr, topY, bottomY, down, sizeof(down), line, sz, ch);
}

void scr_scroll_up(
        screen* scr, char topY, char bottomY, char* line, int sz, char ch) {
    static const char up[] = {23, 7, 0, 3, 8};
    scroll_region(scr, topY, bottomY, up, sizeof(up), line, sz, ch);
}

void scr_erase(screen* scr, int sz) {
    sz = sz + scr->currX_;
    if (sz > scr->cols_) {
        sz = scr->cols_;
    }
    out_run(' ', sz - scr->currX_);
    out_flush();
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

