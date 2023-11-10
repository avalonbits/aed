#include "screen.h"

#include <agon/vdp_vdu.h>
#include <string.h>
#include <mos_api.h>
#include <stdio.h>

#define MAX_COLS 256
static void set_colours(uint8_t fg, uint8_t bg) {
    vdp_set_text_colour(fg);
    vdp_set_text_colour(bg+128);
}

void scr_show_cursor_ch(screen* scr, uint8_t ch) {
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

static void get_active_colours(screen* scr) {
    scr->bg_ = 0;
    scr->fg_ = 15;
    set_colours(scr->fg_, scr->bg_);
}

screen *scr_init(screen* scr, char cursor) {
    vdp_cursor_enable(false);
    get_active_colours(scr);
    scr->rows_ = getsysvar_scrRows();
    scr->cols_ = getsysvar_scrCols();
    scr->cursor_ = cursor;
    scr->topY_ = 1;
    scr->bottomY_ = scr->rows_-2;
    scr->tab_size_ = 4;
    scr_clear(scr);
    scr_show_cursor(scr);
    vdp_cursor_home();

    return scr;
}

void scr_destroy(screen* scr) {
    vdp_cursor_enable(true);
    scr->currX_ = 0;
    scr->currY_ = 0;
    scr->rows_ = 0;
    scr->cols_ = 0;
}

void scr_footer(screen* scr, int x, int y) {
    vdp_cursor_tab(scr->bottomY_, 0);
    putch(' ');
    set_colours(scr->bg_, scr->fg_);
    for (int i = 0; i < scr->cols_-13; i++) {
        putch(' ');
    }
    printf("%4d,%-6d", y, x);
    set_colours(scr->fg_, scr->bg_);
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

const char* title = "AED: Another Text Editor";
void scr_clear(screen* scr) {
    vdp_clear_screen();
    vdp_cursor_home();
    const int len = strlen(title);
    const int banner = (scr->cols_ - len)/2;
    for (int i = 0; i < banner; i++) {
        putch('-');
    }
    set_colours(scr->bg_, scr->fg_);
    printf("%s",title);
    set_colours(scr->fg_, scr->bg_);
    for (int i = 0; i < banner; i++)  {
        putch('-');
    }
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

static void scr_hide_cursor_ch(screen* scr, uint8_t ch) {
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

void scr_putc(screen* scr, uint8_t ch, uint8_t* prefix, int psz, uint8_t* suffix, int ssz) {
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

static void print_suffix(screen* scr, uint8_t* suffix, int sz) {
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

void scr_del(screen* scr, uint8_t* suffix, int sz) {
    uint8_t ch = scr->cursor_;
    if (sz > 0) {
        ch = suffix[0];
        print_suffix(scr, suffix, sz);
    }
    scr_show_cursor_ch(scr, ch);
}

void scr_bksp(screen* scr, uint8_t* suffix, int sz) {
    if (scr->currX_ == 0) {
        return;
    }
    scr->currX_--;
    scr_hide_cursor(scr);
    vdp_cursor_tab(scr->currY_, scr->currX_);

    uint8_t ch = scr->cursor_;
    if (sz > 0) {
        ch = suffix[0];
        print_suffix(scr, suffix, sz);
    }
    scr_show_cursor_ch(scr, ch);
}

void scr_left(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX, uint8_t* suffix, int sz) {
    int x = scr->currX_ - deltaX;
    if (x >= 0) {
        scr->currX_ -= deltaX;
    } else if (sz > 0) {
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

void scr_right(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX, uint8_t* prefix, int sz) {
    int x = scr->currX_ + deltaX;
    if (x < scr->cols_) {
        scr->currX_ += deltaX;
    } else if (sz > 0) {
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

void scr_home(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t* suffix, int sz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currX_ = 0;
    if (sz > 0) {
        scr_write_line(scr, scr->currY_, suffix, sz);
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_end(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t deltaX, uint8_t* prefix, int sz) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currX_ += deltaX;
    if (scr->currX_ >= scr->cols_) {
        scr->currX_ = scr->cols_-1;
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

void scr_up(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_--;
    scr->currX_ = currX;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_down(screen* scr, uint8_t from_ch, uint8_t to_ch, uint8_t currX) {
    scr_hide_cursor_ch(scr, from_ch);
    scr->currY_++;
    scr->currX_ = currX;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_write_line(screen* scr, uint8_t ypos, uint8_t* buf, int sz) {
    scr_overwrite_line(scr, ypos, buf, sz, scr->cols_);
}

void scr_overwrite_line(screen* scr, uint8_t ypos, uint8_t* buf, int sz, int psz) {
    vdp_cursor_tab(ypos, 0);
    int i = 0;
    for (; i < sz && i < scr->cols_; i++) {
        putch(buf[i]);
    }
    for (; i < psz && i < scr->cols_; ++i) {
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

