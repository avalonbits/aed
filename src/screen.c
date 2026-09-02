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

void set_colours(char fg, char bg) {
    vdp_set_text_colour(fg);
    vdp_set_text_colour(bg+128);
}

void scr_show_cursor_ch(screen* scr, char ch) {
    if (ch == 0 || ch == '\r' || ch == '\n') {
        ch = scr->cursor_;
    }

    // First reverse colors
    set_colours(scr->bg_, scr->fg_);

    // Print the cursor;
    putchar(ch);
    vdp_cursor_left();

    // Reverse colors back.
    set_colours(scr->fg_, scr->bg_);
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
    set_colours(scr->fg_, scr->bg_);
}

screen *scr_init(screen* scr, char cursor) {
    static char disable_cursor_wrap[4] = {23, 16, 1, 0};
    VDP_PUTS(disable_cursor_wrap);

    vdp_cursor_enable(false);
    scr->rows_ = getsysvar_scrRows();
    scr->cols_ = getsysvar_scrCols();
    scr->colors_ = getsysvar_scrColours();
    scr->cursor_ = cursor;
    scr->topY_ = 1;
    scr->bottomY_ = scr->rows_-1;
    get_active_colours(scr);
    scr_clear(scr);
    scr_show_cursor(scr);
    vdp_cursor_home();
    scr->tab_size_ = SCR_DEFAULT_TAB_SIZE;

    return scr;
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

void scr_place_cursor(screen* scr, const char* line, int len) {
    int col = scr_column_of(scr, line, len);
    if (col > scr->cols_ - 1) {
        col = scr->cols_ - 1;
    }
    if (col < 0) {
        col = 0;
    }
    scr->currX_ = col;
}

void scr_destroy(screen* scr) {
    static char enable_cursor_wrap[4] = {23, 16, 1, 1};
    VDP_PUTS(enable_cursor_wrap);
    vdp_cursor_enable(true);
    scr->currX_ = 0;
    scr->currY_ = 0;
    scr->rows_ = 0;
    scr->cols_ = 0;
}

void scr_footer(screen* scr, char* fname, bool dirty, int x, int y) {
    static char* no_file = "[NO FILE]";
    if (fname == NULL) {
        fname = no_file;
    }
    const int fnsz = strlen(fname);
    int psz = 13 + fnsz ;

    vdp_cursor_tab(0, scr->bottomY_);
    set_colours(scr->bg_, scr->fg_);

    mos_puts(fname, fnsz, 0);
    if (dirty) {
        putchar('*');
    } else {
        putchar(' ');
    }
    putchar(' ');
    for (int i = 0; i < scr->cols_-psz; i++) {
        putchar(' ');
    }

    static char digits[16];
    i2s(y, digits, 16);
    int dsz = strlen(digits);
    char max = strlen(digits) < 4 ? 4 - strlen(digits) : 4;
    for (int i = 0; i < max; i++) {
        putchar(' ');
    }
    mos_puts(digits, dsz, 0);
    putchar(',');

    i2s(x, digits, 16);
    dsz = strlen(digits);
    max = strlen(digits) < 6 ? 6 - strlen(digits) : 6;
    mos_puts(digits, dsz, 0);
    for (int i = 0; i < max; i++) {
        putchar(' ');
    }

    set_colours(scr->fg_, scr->bg_);
    vdp_cursor_tab(scr->currX_, scr->currY_ );
}

char* title = "AED: Another Text Editor";
void scr_clear(screen* scr) {
    vdp_clear_screen();
    vdp_cursor_home();
    vdp_cursor_tab(0,0);
    const int len = strlen(title);
    const int banner = (scr->cols_ - len)/2;
    for (int i = 0; i < banner; i++) {
        putchar('-');
    }
    set_colours(scr->bg_, scr->fg_);
    mos_puts(title, strlen(title), 0);
    set_colours(scr->fg_, scr->bg_);
    for (int i = 0; i < banner; i++)  {
        putchar('-');
    }
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

void scr_hide_cursor_ch(screen* scr, char ch) {
    if (ch == 0 || ch == '\r' || ch == '\n') {
        ch = scr->cursor_;
    }

    set_colours(scr->fg_, scr->bg_);
    putchar(ch);
    vdp_cursor_left();
}

static void scr_hide_cursor(screen* scr) {
    scr_hide_cursor_ch(scr, scr->cursor_);
}

void scr_putc(screen* scr, char ch, char* prefix, int psz, char* suffix, int ssz) {
    scr_hide_cursor(scr);
    if (scr->currX_ < scr->cols_-1) {
        putchar(ch);
        scr->currX_++;
        if (suffix != NULL && ssz > 0) {
            int max = scr->cols_ - scr->currX_;
            for (int i = 0; i < ssz && i < max; i++) {
                putchar(suffix[i]);
            }
            vdp_cursor_tab(scr->currX_, scr->currY_);
            scr_show_cursor_ch(scr, suffix[0]);
        } else {
            scr_show_cursor(scr);
        }
    } else {
        int pad = psz - scr->cols_+1;
        scr_write_line(scr, scr->currY_, prefix+pad, psz-pad-1);
        vdp_cursor_tab(scr->currX_ -1, scr->currY_);
        putchar(ch);
        if (ssz > 0) {
            scr_show_cursor_ch(scr, suffix[0]);
        } else {
            scr_show_cursor(scr);
        }
    }
}

static void print_suffix(screen* scr, char* suffix, int sz) {
    int i = 0;
    const int limit = scr->cols_ - scr->currX_;
    for (; i < sz && i < limit; i++) {
        putchar(suffix[i]);
    }
    if (i < limit) {
        putchar(' ');
    }
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

void scr_del(screen* scr, char* suffix, int sz) {
    char ch = scr->cursor_;
    if (sz > 0) {
        ch = suffix[0];
        print_suffix(scr, suffix, sz);
    }
    scr_show_cursor_ch(scr, ch);
}

void scr_bksp(screen* scr, char* suffix, int sz) {
    if (scr->currX_ == 0) {
        return;
    }
    scr->currX_--;
    scr_hide_cursor(scr);
    vdp_cursor_tab(scr->currX_, scr->currY_);

    char ch = scr->cursor_;
    if (sz > 0) {
        ch = suffix[0];
        print_suffix(scr, suffix, sz);
    }
    scr_show_cursor_ch(scr, ch);
}

void scr_left(screen* scr, char from_ch, char to_ch, int deltaX, char* suffix, int sz) {
    int x = scr->currX_ - deltaX;
    if (x >= 0) {
        scr->currX_ -= deltaX;
    } else if (sz > 0) {
        scr->currX_ = 0;
        int max = scr->cols_ - scr->currX_;
        vdp_cursor_tab(0, scr->currY_);
        for (int i = 0; i < max; i++) {
            putchar(suffix[i]);
        }
        vdp_cursor_tab(scr->currX_, scr->currY_);

    }
    scr_hide_cursor_ch(scr, from_ch);
    vdp_cursor_tab(scr->currX_, scr->currY_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_right(screen* scr, char from_ch, char to_ch, int deltaX, char* prefix, int sz) {
    int x = scr->currX_ + deltaX;
    if (x < scr->cols_) {
        scr->currX_ = x;
    } else if (sz > 0) {
        scr->currX_ = scr->cols_-1;
        int pad = sz - scr->cols_ + 1;

        vdp_cursor_tab(0, scr->currY_);
        for (int i = 0; i < scr->cols_; i++) {
            putchar(prefix[i+pad]);
        }
        vdp_cursor_tab(scr->currX_, scr->currY_);

    }
    scr_hide_cursor_ch(scr, from_ch);
    vdp_cursor_tab(scr->currX_, scr->currY_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_home(screen* scr, char from_ch, char to_ch, char* suffix, int sz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currX_ = 0;
    if (sz > 0) {
        scr_write_line(scr, scr->currY_, suffix, sz);
    }
    vdp_cursor_tab(scr->currX_, scr->currY_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_end(screen* scr, char from_ch, char to_ch, int deltaX, char* prefix, int sz) {
    scr_hide_cursor_ch(scr, from_ch);
    int x = (scr->currX_) + deltaX;
    if (x >= scr->cols_) {
        scr->currX_ = scr->cols_-1;
    } else {
        scr->currX_ = x;
    }
    if (sz > 0) {
        int pad = 0;
        if (sz >= scr->cols_) {
            pad = sz - scr->cols_+1;
        }
        scr_write_line(scr, scr->currY_, prefix+pad, sz-pad);
    }
    vdp_cursor_tab(scr->currX_, scr->currY_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_up(screen* scr, char from_ch, char to_ch, const char* line, int len) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_--;
    scr_place_cursor(scr, line, len);
    vdp_cursor_tab(scr->currX_, scr->currY_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_down(screen* scr, char from_ch, char to_ch, const char* line, int len) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_++;
    scr_place_cursor(scr, line, len);
    vdp_cursor_tab(scr->currX_, scr->currY_);
    scr_show_cursor_ch(scr, to_ch);
}

// VDU 28, left, bottom, right, top -- define a text viewport.
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
    define_viewport(0, bottom, scr->cols_, top);
    vdp_clear_screen();
    reset_viewport();
}

void scr_write_line(screen* scr, char ypos, char* buf, int sz) {
    scr_overwrite_line(scr, ypos, buf, sz, scr->cols_);
}

void scr_overwrite_line(screen* scr, char ypos, char* buf, int sz, int psz) {
    vdp_cursor_tab(0, ypos);
    int i = 0;
    for (; i < sz && i < scr->cols_; i++) {
        putchar(buf[i]);
    }
    for (; i < psz && i < scr->cols_; i++) {
        putchar(' ');
    }
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

void scr_sync_cursor(screen* scr) {
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

// VDU 23,7,extent,direction,movement -- scroll the current text viewport by one
// character row. Direction 2 is down, 3 is up.
static void scroll_region(
        screen* scr, char topY, char bottomY, const char* vdu, char sz,
        char* line, int lsz, char ch) {
    define_viewport(0, bottomY, scr->cols_, topY);
    mos_puts((char*) vdu, sz, 0);
    reset_viewport();
    scr_write_line(scr, scr->currY_, line, lsz);
    scr_sync_cursor(scr);
    scr_show_cursor_ch(scr, ch);
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
    for (int i = scr->currX_; i < sz; ++i) {
        putchar(' ');
    }
    vdp_cursor_tab(scr->currX_, scr->currY_);
}

