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

#include <agon/vdp_vdu.h>
#include <string.h>
#include <mos_api.h>
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
    putch(ch);
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
    putch(ch);

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
    scr->tab_size_ = 4;

    return scr;
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

void scr_footer(screen* scr, char* fname, bool select_mode, bool dirty, int x, int y) {
    static char* no_file = "[NO FILE]";
    if (fname == NULL) {
        fname = no_file;
    }
    const int fnsz = strlen(fname);
    int psz = 13 + fnsz ;

    vdp_cursor_tab(scr->bottomY_, 0);
    if (select_mode) {
        set_colours(scr->fg_, scr->bg_);
    } else {
        set_colours(scr->bg_, scr->fg_);
    }

    mos_puts(fname, fnsz, 0);
    if (dirty) {
        putch('*');
    } else {
        putch(' ');
    }
    putch(' ');
    for (int i = 0; i < scr->cols_-psz; i++) {
        putch(' ');
    }

    static char digits[16];
    i2s(y, digits, 16);
    int dsz = strlen(digits);
    char max = strlen(digits) < 4 ? 4 - strlen(digits) : 4;
    for (int i = 0; i < max; i++) {
        putch(' ');
    }
    mos_puts(digits, dsz, 0);
    putch(',');

    i2s(x, digits, 16);
    dsz = strlen(digits);
    max = strlen(digits) < 6 ? 6 - strlen(digits) : 6;
    mos_puts(digits, dsz, 0);
    for (int i = 0; i < max; i++) {
        putch(' ');
    }

    set_colours(scr->fg_, scr->bg_);
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

char* title = "AED: Another Text Editor";
void scr_clear(screen* scr) {
    vdp_clear_screen();
    vdp_cursor_home();
    vdp_cursor_tab(0,0);
    const int len = strlen(title);
    const int banner = (scr->cols_ - len)/2;
    for (int i = 0; i < banner; i++) {
        putch('-');
    }
    set_colours(scr->bg_, scr->fg_);
    mos_puts(title, strlen(title), 0);
    set_colours(scr->fg_, scr->bg_);
    for (int i = 0; i < banner; i++)  {
        putch('-');
    }
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

void scr_hide_cursor_ch(screen* scr, char ch) {
    if (ch == 0 || ch == '\r' || ch == '\n') {
        ch = scr->cursor_;
    }

    set_colours(scr->fg_, scr->bg_);
    putch(ch);
    vdp_cursor_left();
}

static void scr_hide_cursor(screen* scr) {
    scr_hide_cursor_ch(scr, scr->cursor_);
}

void scr_putc(screen* scr, char ch, char* prefix, int psz, char* suffix, int ssz) {
    scr_hide_cursor(scr);
    if (scr->currX_ < scr->cols_-1) {
        putch(ch);
        scr->currX_++;
        if (suffix != NULL && ssz > 0) {
            int max = scr->cols_ - scr->currX_;
            for (int i = 0; i < ssz && i < max; i++) {
                putch(suffix[i]);
            }
            vdp_cursor_tab(scr->currY_, scr->currX_);
            scr_show_cursor_ch(scr, suffix[0]);
        } else {
            scr_show_cursor(scr);
        }
    } else {
        int pad = psz - scr->cols_+1;
        scr_write_line(scr, scr->currY_, prefix+pad, psz-pad-1);
        vdp_cursor_tab(scr->currY_, scr->currX_-1);
        putch(ch);
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
        putch(suffix[i]);
    }
    if (i < limit) {
        putch(' ');
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
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
    vdp_cursor_tab(scr->currY_, scr->currX_);

    char ch = scr->cursor_;
    if (sz > 0) {
        ch = suffix[0];
        print_suffix(scr, suffix, sz);
    }
    scr_show_cursor_ch(scr, ch);
}

void scr_left(screen* scr, char from_ch, char to_ch, char deltaX, char* suffix, int sz) {
    int x = scr->currX_ - deltaX;
    if (x >= 0) {
        scr->currX_ -= deltaX;
    } else if (sz > 0) {
        scr->currX_ = 0;
        int max = scr->cols_ - scr->currX_;
        vdp_cursor_tab(scr->currY_, 0);
        for (int i = 0; i < max; i++) {
            putch(suffix[i]);
        }
        vdp_cursor_tab(scr->currY_, scr->currX_);

    }
    scr_hide_cursor_ch(scr, from_ch);
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_right(screen* scr, char from_ch, char to_ch, char deltaX, char* prefix, int sz) {
    int x = scr->currX_ + deltaX;
    if (x < scr->cols_) {
        scr->currX_ = x;
    } else if (sz > 0) {
        scr->currX_ = scr->cols_-1;
        int pad = sz - scr->cols_ + 1;

        vdp_cursor_tab(scr->currY_, 0);
        for (int i = 0; i < scr->cols_; i++) {
            putch(prefix[i+pad]);
        }
        vdp_cursor_tab(scr->currY_, scr->currX_);

    }
    scr_hide_cursor_ch(scr, from_ch);
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_home(screen* scr, char from_ch, char to_ch, char* suffix, int sz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currX_ = 0;
    if (sz > 0) {
        scr_write_line(scr, scr->currY_, suffix, sz);
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
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
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_up(screen* scr, char from_ch, char to_ch, char currX) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_--;
    scr->currX_ = currX;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_down(screen* scr, char from_ch, char to_ch, char currX) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_++;
    scr->currX_ = currX;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

static void define_viewport(screen* scr, char top, char bottom) {
    static char viewport[5] = {28, 0, 0, 0, 0};
    viewport[2] = bottom;
    viewport[3] = scr->cols_;
    viewport[4] = top;
    VDP_PUTS(viewport);
}

void scr_clear_textarea(screen* scr, char top, char bottom) {
    define_viewport(scr, top, bottom);
    vdp_clear_screen();
    putch(26);  // Reset viewport.
}

void scr_write_line(screen* scr, char ypos, char* buf, int sz) {
    scr_overwrite_line(scr, ypos, buf, sz, scr->cols_);
}

void scr_overwrite_line(screen* scr, char ypos, char* buf, int sz, int psz) {
    vdp_cursor_tab(ypos, 0);
    int i = 0;
    for (; i < sz && i < scr->cols_; i++) {
        putch(buf[i]);
    }
    for (; i < psz && i < scr->cols_; i++) {
        putch(' ');
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

void scr_erase(screen* scr, int sz) {
    sz = sz + scr->currX_;
    if (sz > scr->cols_) {
        sz = scr->cols_;
    }
    for (int i = scr->currX_; i < sz; ++i) {
        putch(' ');
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

