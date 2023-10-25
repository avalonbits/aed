#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <stdio.h>

#define UN(x) (void)(x)

void cmd_noop(screen* scr, text_buffer* buf) {
    UN(scr);UN(buf);
}

void cmd_putc(screen* scr, text_buffer* buf, key k) {
	tb_put(buf, k.key);
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_putc(scr, k.key, suffix, sz);
}

void cmd_del(screen* scr, text_buffer* buf) {
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

void cmd_bksp(screen* scr, text_buffer* buf) {
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

static void scroll_lines(screen* scr, text_buffer* buf, uint8_t ch) {
    if (scr->currY_ < scr->bottomY_-1) {
        scr_clear_suffix(scr);
        line_itr next = tb_nline(buf);
        uint8_t ypos = scr->currY_+1;
        for (line l = next(); l.b != NULL && ypos < scr->bottomY_; l = next(), ++ypos) {
            scr_write_line(scr, ypos, l.b, l.sz);
        }
        scr->currY_++;
    } else {
        line_itr prev = tb_pline(buf);
        uint8_t ypos = scr->currY_;
        for (line l = prev(); ypos >= scr->topY_; l = prev(), --ypos) {
            scr_write_line(scr, ypos, l.b, l.sz);
        }
    }
    scr->currX_ = 0;
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_home(scr, ch, ch);
}

void cmd_newl(screen* scr, text_buffer* buf) {
    uint8_t ch = tb_peek(buf);
    if (!tb_newline(buf)) {
        return;
    }
    scroll_lines(scr, buf, ch);
}

void cmd_left(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_prev(buf);
    scr_left(scr, from_ch, to_ch, 1);
}

void cmd_w_left(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }
    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_prev(buf);
    const uint8_t deltaX = from_x - tb_xpos(buf);
    scr_left(scr, from_ch, to_ch, deltaX);
}

void cmd_right(screen* scr, text_buffer* buf) {
    if (tb_xpos(buf) >= scr->cols_ || tb_eol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    if (from_ch == 0 ) {
        return;
    }
    uint8_t to_ch = tb_next(buf);
    scr_right(scr, from_ch, to_ch, 1);
}

void cmd_w_right(screen* scr, text_buffer* buf) {
    if (tb_xpos(buf) >= scr->cols_ || tb_eol(buf)) {
        return;
    }

    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_next(buf);
    const uint8_t deltaX = tb_xpos(buf) - from_x;
    scr_right(scr, from_ch, to_ch, deltaX);
}

void cmd_up(screen* scr, text_buffer* buf) {
    if (tb_ypos(buf) == 1) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_up(buf);
    scr_up(scr, from_ch, to_ch, tb_xpos(buf)-1);
}

void cmd_down(screen* scr, text_buffer* buf) {
    tb_down(buf);
}

void cmd_home(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_home(buf);
    if (to_ch != 0) {
        scr_home(scr, from_ch, tb_peek(buf));
    }
}

void cmd_end(screen* scr, text_buffer* buf) {
    const uint8_t from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_end(buf);
    const uint8_t deltaX = tb_xpos(buf) - from_x;
    if (deltaX > 0) {
        scr_end(scr, from_ch, to_ch, deltaX);
    }
}
