#include "screen.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>

static void set_colours(uint8_t fg, uint8_t bg) {
    vdp_set_text_colour(fg);
    vdp_set_text_colour(bg+128);
}

static void scr_show_cursor_ch(screen* scr, uint8_t ch) {
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

screen *scr_init(screen* scr, char cursor, char fg, char bg) {
    vdp_vdu_init();
    vdp_cursor_enable(false);
    scr->rows_ = getsysvar_scrRows();
    scr->cols_ = getsysvar_scrCols();
    scr->cursor_ = cursor;
    scr->fg_ = fg;
    scr->bg_ = bg;
    scr->topY_ = 1;
    scr->bottomY_ = scr->rows_-1;
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
    printf("                                                                 %6d,%-6d", x, y);
    set_colours(scr->fg_, scr->bg_);
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

const char* title = "AED - Agon Text Editor";
void scr_clear(screen* scr) {
    vdp_clear_screen();
    vdp_cursor_home();
    set_colours(scr->fg_, scr->bg_);
    printf("-----------------------------");
    set_colours(scr->bg_, scr->fg_);
    printf("%s",title);
    set_colours(scr->fg_, scr->bg_);
    printf("-----------------------------");
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

static void scr_hide_cursor_ch(screen* scr, uint8_t ch) {
    set_colours(scr->fg_, scr->bg_);
    putch(ch);
    vdp_cursor_left();
}

static void scr_hide_cursor(screen* scr) {
    scr_hide_cursor_ch(scr, scr->cursor_);
}

void scr_putc(screen* scr, uint8_t ch, uint8_t* suffix, int sz) {
    scr_hide_cursor(scr);
    putch(ch);
    scr->currX_++;
    if (suffix != NULL && sz > 0) {
        for (int i = 0; i < sz; i++) {
            putch(suffix[i]);
        }
        vdp_cursor_tab(scr->currY_, scr->currX_);
        scr_show_cursor_ch(scr, suffix[0]);
    } else {
        scr_show_cursor(scr);
    }
}

void scr_del(screen* scr, uint8_t* suffix, int sz) {
    scr_erase(scr, sz);
    if (sz > 1) {
        for (int i = 1; i < sz; i++) {
            putch(suffix[i]);
        }
        vdp_cursor_tab(scr->currY_, scr->currX_);
        scr_show_cursor_ch(scr, suffix[1]);
    } else {
        scr_show_cursor(scr);
    }
}

void scr_bksp(screen* scr, uint8_t* suffix, int sz) {
    if (scr->currX_ == 0) {
        return;
    }
    scr_erase(scr, sz);
    scr->currX_--;
    scr_hide_cursor(scr);
    vdp_cursor_left();
    if (suffix != NULL && sz > 0) {
        for (int i = 0; i < sz; i++) {
            putch(suffix[i]);
        }
        vdp_cursor_tab(scr->currY_, scr->currX_);
        scr_show_cursor_ch(scr, suffix[0]);
    } else {
        scr_show_cursor(scr);
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
}

void scr_left(screen* scr, uint8_t from_ch, uint8_t to_ch) {
    if (scr->currX_ == 0) {
        return;
    }
    scr->currX_--;
    if (from_ch == 0) {
        from_ch = scr->cursor_;
    }
    scr_hide_cursor_ch(scr, from_ch);
    vdp_cursor_left();
    scr_show_cursor_ch(scr, to_ch);
}

void scr_right(screen* scr, uint8_t from_ch, uint8_t to_ch) {
    scr->currX_++;
    if (to_ch == 0) {
        to_ch = scr->cursor_;
    }
    scr_hide_cursor_ch(scr, from_ch);
    vdp_cursor_right();
    scr_show_cursor_ch(scr, to_ch);
}

void scr_home(screen* scr, uint8_t from_ch, uint8_t to_ch) {
    if (from_ch == 0) {
        from_ch = scr->cursor_;
    }
    scr_hide_cursor_ch(scr, from_ch);
    scr->currX_ = 0;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, to_ch);
}

void scr_end(screen* scr) {
}

void scr_erase(screen* scr, int sz) {
    for (int i = 0; i < sz; i++) {
        putch(' ');
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
}
