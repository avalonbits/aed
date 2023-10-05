#include "screen.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>

screen *scr_init(screen* scr, char cursor) {
    vdp_cursor_enable(false);
    scr->rows_ = getsysvar_scrRows();
    scr->cols_ = getsysvar_scrCols();
    scr->cursor_ = cursor;
    scr_clear(scr);
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

void scr_putc(screen* scr, char ch) {
    putch(ch);
    scr->currX_++;
}
