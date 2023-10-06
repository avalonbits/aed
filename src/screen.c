#include "screen.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>


static void scr_show_cursor(screen* scr) {
    // First reverse colors
    vdp_set_text_colour(scr->bg_);
    vdp_set_text_colour(scr->fg_+128);

    // Print the cursor;
    putch(scr->cursor_);
    vdp_cursor_left();

    // Reverse colors back.
    vdp_set_text_colour(scr->fg_);
    vdp_set_text_colour(scr->bg_+128);
}

screen *scr_init(screen* scr, char cursor, char fg, char bg) {
    vdp_vdu_init();
    vdp_cursor_enable(false);
    scr->rows_ = getsysvar_scrRows();
    scr->cols_ = getsysvar_scrCols();
    scr->cursor_ = cursor;
    scr->fg_ = fg;
    scr->bg_ = bg;
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

void scr_clear(screen* scr) {
    vdp_clear_screen();
    vdp_cursor_home();

    scr->currX_ = 0;
    scr->currY_ = 0;
}

static void scr_hide_cursor(screen* scr) {
    vdp_set_text_colour(scr->fg_);
    vdp_set_text_colour(scr->bg_+128);
    putch(scr->cursor_);
    vdp_cursor_left();
}

void scr_putc(screen* scr, char ch) {
    scr_hide_cursor(scr);
    putch(ch);
    scr_show_cursor(scr);
    scr->currX_++;
}

void scr_bksp(screen* scr) {
    scr_hide_cursor(scr);
    vdp_cursor_left();
    scr_show_cursor(scr);
}

