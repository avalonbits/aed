#include "screen.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>


static void scr_show_cursor_ch(screen* scr, uint8_t ch) {
    // First reverse colors
    vdp_set_text_colour(scr->bg_);
    vdp_set_text_colour(scr->fg_+128);

    // Print the cursor;
    putch(ch);
    vdp_cursor_left();

    // Reverse colors back.
    vdp_set_text_colour(scr->fg_);
    vdp_set_text_colour(scr->bg_+128);
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
    scr->bottomY_ = scr->cols_-2;
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

static void set_colours(uint8_t fg, uint8_t bg) {
    vdp_set_text_colour(fg);
    vdp_set_text_colour(bg+128);
}

static void scr_set_colours(screen* scr) {
    vdp_set_text_colour(scr->fg_);
    vdp_set_text_colour(scr->bg_+128);
}

const char* title = "AED - Agon Text Editor";
void scr_clear(screen* scr) {
    vdp_clear_screen();
    vdp_cursor_home();
    scr_set_colours(scr);
    printf("-----------------------------");
    set_colours(scr->bg_, scr->fg_);
    printf("%s",title);
    scr_set_colours(scr);
    printf("-----------------------------");
    scr->currX_ = 0;
    scr->currY_ = scr->topY_;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_set_colours(scr);
}

static void scr_hide_cursor_ch(screen* scr, uint8_t ch) {
    vdp_set_text_colour(scr->fg_);
    vdp_set_text_colour(scr->bg_+128);
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

void scr_bksp(screen* scr, uint8_t* suffix, int sz) {
    if (scr->currX_ == 0) {
        return;
    }
    scr_erase(scr, sz);
    scr->currX_--;
    scr_hide_cursor(scr);
    vdp_cursor_left();
    scr_show_cursor(scr);
    for (int i = 0; i < sz; i++) {
        putch(suffix[i]);
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

void scr_erase(screen* scr, int sz) {
    for (int i = 0; i < sz; i++) {
        putch(' ');
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
}
