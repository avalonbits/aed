#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <stdio.h>

#define UN(x) (void)(x)

void cmd_noop(screen* scr, text_buffer* buf, key k) {
    UN(scr);UN(buf);UN(k);
}

void cmd_putc(screen* scr, text_buffer* buf, key k) {
	tb_put(buf, k.key);
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_putc(scr, k.key, suffix, sz);
}

void cmd_del(screen* scr, text_buffer* buf, key k) {
    UN(k);

    if (tb_eol(buf)) {
        return;
    }
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    if (!tb_del(buf)) {
        return;
    }
    scr_del(scr, suffix, sz);
}

void cmd_bksp(screen* scr, text_buffer* buf, key k) {
    UN(k);
    if (tb_bol(buf)) {
        return;
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_erase(scr, sz);

    if (!tb_bksp(buf)) {
        return;
    }
    scr_bksp(scr, suffix, sz);
}

void cmd_newl(screen* scr, text_buffer* buf, key k) {
    UN(k);

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    if (tb_newline(buf)) {
        scr_newl(scr, suffix, sz);
    }
}

void cmd_left(screen* scr, text_buffer* buf, key k) {
    UN(k);

    if (tb_bol(buf)) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_prev(buf);
    scr_left(scr, from_ch, to_ch, 1);
}

void cmd_w_left(screen* scr, text_buffer* buf, key k) {
    UN(k);

    if (tb_bol(buf)) {
        return;
    }
    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_prev(buf);
    scr_left(scr, from_ch, to_ch, from_x - tb_xpos(buf));
}

void cmd_right(screen* scr, text_buffer* buf, key k) {
    UN(k);

    if (tb_xpos(buf) >= scr->cols_ || tb_eol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    if (from_ch == 0 ) {
        return;
    }
    uint8_t to_ch = tb_next(buf);
    scr_right(scr, from_ch, to_ch);
}

void cmd_up(screen* scr, text_buffer* buf, key k) {
    UN(k);

    if (tb_ypos(buf) == 1) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_up(buf);
    scr_up(scr, from_ch, to_ch, tb_xpos(buf)-1);
}

void cmd_down(screen* scr, text_buffer* buf, key k) {
    UN(k);

    tb_down(buf);
}

void cmd_home(screen* scr, text_buffer* buf, key k) {
    UN(k);

    if (tb_bol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_home(buf);
    if (to_ch != 0) {
        scr_home(scr, from_ch, tb_peek(buf));
    }
}

void cmd_end(screen* scr, text_buffer* buf, key k) {
    UN(k);

    const uint8_t from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_end(buf);
    const uint8_t deltaX = tb_xpos(buf) - from_x;
    if (deltaX > 0) {
        scr_end(scr, from_ch, to_ch, deltaX);
    }
}
