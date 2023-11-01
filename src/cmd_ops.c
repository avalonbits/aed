#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>

#define UN(x) (void)(x)

void cmd_noop(screen* scr, text_buffer* buf) {
    UN(scr);UN(buf);
}

void cmd_save(const char* fname, text_buffer* buf) {
    uint8_t fh = mos_fopen(fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return;
    }

    uint8_t* prefix = NULL;
    uint8_t* suffix = NULL;
    int psz = 0;
    int ssz = 0;
    tb_content(buf, &prefix, &psz, &suffix, &ssz);

    if (prefix != NULL && psz > 0) {
        mos_fwrite(fh, prefix, psz);
    }
    if (suffix != NULL && ssz > 0) {
        mos_fwrite(fh, suffix, ssz);
    }
    mos_fclose(fh);

}

void cmd_quit(screen* scr, text_buffer* buf) {
    cmd_save(buf->fname_, buf);
}

void cmd_putc(screen* scr, text_buffer* buf, key k) {
	tb_put(buf, k.key);

    int psz = 0;
    uint8_t* prefix = tb_prefix(buf, &psz);
    int ssz = 0;
    uint8_t* suffix = tb_suffix(buf, &ssz);
    scr_putc(scr, k.key, prefix, psz, suffix, ssz);
}

void cmd_del(screen* scr, text_buffer* buf) {
    if (tb_eol(buf)) {
        return;
    }
    if (!tb_del(buf)) {
        return;
    }
    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_del(scr, suffix, sz);
}

void cmd_bksp(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);

    if (!tb_bksp(buf)) {
        return;
    }
    scr_bksp(scr, suffix, sz);
}

static void scroll_lines(screen* scr, text_buffer* buf, uint8_t ch) {
    if (scr->currY_ < scr->bottomY_-1) {
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
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

void cmd_newl(screen* scr, text_buffer* buf) {
    uint8_t ch = tb_peek(buf);
    int sz = 0;
    uint8_t* prefix = tb_prefix(buf, &sz);
    if (!tb_newline(buf)) {
        return;
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_write_line(scr, scr->currY_, prefix, sz);
    scr->currX_ = 0;
    scroll_lines(scr, buf, ch);
}

void cmd_left(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_prev(buf);

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_left(scr, from_ch, to_ch, 1, suffix, sz);
}

void cmd_w_left(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }
    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_prev(buf);
    const uint8_t deltaX = from_x - tb_xpos(buf);
    scr_left(scr, from_ch, to_ch, deltaX, NULL, 0);
}

void cmd_right(screen* scr, text_buffer* buf) {
    if (tb_eol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    if (from_ch == 0 ) {
        return;
    }
    uint8_t to_ch = tb_next(buf);

    int sz = 0;
    uint8_t* prefix = tb_prefix(buf, &sz);
    scr_right(scr, from_ch, to_ch, 1, prefix, sz);
}

void cmd_w_right(screen* scr, text_buffer* buf) {
    if (tb_xpos(buf) >= scr->cols_ || tb_eol(buf)) {
        return;
    }

    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_next(buf);
    const uint8_t deltaX = tb_xpos(buf) - from_x;
    scr_right(scr, from_ch, to_ch, deltaX, NULL, 0);
}

void cmd_up(screen* scr, text_buffer* buf) {
    int ypos = tb_ypos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_up(buf);
    if (ypos == tb_ypos(buf)) {
        return;
    }

    if (scr->currY_ > scr->topY_) {
        scr_up(scr, from_ch, to_ch, tb_xpos(buf)-1);
    } else {
        scroll_lines(scr, buf, to_ch);
    }
}

void cmd_down(screen* scr, text_buffer* buf) {
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_down(buf);
    if (scr->currY_ == tb_ypos(buf)) {
        return;
    }
    if (scr->currY_ >= scr->bottomY_-1) {
        scroll_lines(scr, buf, to_ch);
    } else {
        scr_down(scr, from_ch, to_ch, tb_xpos(buf)-1);
    }
}

void cmd_home(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        return;
    }

    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_home(buf);
    if (to_ch != 0) {
        int sz = 0;
        uint8_t* suffix = tb_suffix(buf, &sz);
        scr_home(scr, from_ch, tb_peek(buf), suffix, sz);
    }
}

void cmd_end(screen* scr, text_buffer* buf) {
    const uint8_t from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_end(buf);
    const uint8_t deltaX = tb_xpos(buf) - from_x;
    if (deltaX > 0) {
        int sz = 0;
        uint8_t* suffix = tb_prefix(buf, &sz);
        scr_end(scr, from_ch, to_ch, deltaX, suffix, sz);
    }
}
