#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>
#include <stdlib.h>

#define UN(x) (void)(x)

void cmd_noop(screen* scr, text_buffer* buf) {
    UN(scr);UN(buf);
}

static bool save_file(text_buffer* buf) {
    if (!tb_valid_file(buf)) {
        return false;
    }

    uint8_t fh = mos_fopen(buf->fname_, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;;
    }

    uint8_t* prefix = NULL;
    uint8_t* suffix = NULL;
    int psz = 0;
    int ssz = 0;
    tb_content(buf, &prefix, &psz, &suffix, &ssz);

    if (prefix != NULL && psz > 0) {
        mos_fwrite(fh, (char*) prefix, psz);
    }
    if (suffix != NULL && ssz > 0) {
        mos_fwrite(fh, (char*) suffix, ssz);
    }
    mos_fclose(fh);
    return true;
}

void cmd_save(text_buffer* buf) {
    save_file(buf);
}

bool cmd_quit(screen* scr, text_buffer* buf) {
    UN(scr);
    if (!tb_valid_file(buf)) {
        return true;
    }
    return save_file(buf);
}

void cmd_putc(screen* scr, text_buffer* buf, key k) {
    if (k.key == '\t') {
        // Convert tab to spaces because it is too damn hard to get it working correctly with line scrolling.
        k.key = ' ';
        for (uint8_t i = 0; i < scr->tab_size_; i++) {
            cmd_putc(scr, buf, k);
        }
        return;
    }

    tb_put(buf, k.key);
    int psz = 0;
    uint8_t* prefix = tb_prefix(buf, &psz);
    int ssz = 0;
    uint8_t* suffix = tb_suffix(buf, &ssz);
    scr_putc(scr, k.key, prefix, psz, suffix, ssz);
}

static void scroll_down_from_top(screen* scr, text_buffer* buf, uint8_t ch) {
    line_itr next = tb_nline(buf, tb_ypos(buf));
    uint8_t ypos = scr->currY_;
    for (line l = next(); l.b != NULL && ypos < scr->bottomY_; l = next(), ++ypos) {
        scr_overwrite_line(scr, ypos, l.b, l.sz, l.osz);
    }
    if (ypos < scr->bottomY_) {
        scr_write_line(scr, ypos, NULL, 0);
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

static void scroll_up_from_top(screen* scr, text_buffer* buf, uint8_t ch) {
    line_itr next = tb_nline(buf, tb_ypos(buf));
    uint8_t ypos = scr->currY_;
    int osz = 0;
    for (line l = next(); l.b != NULL && ypos < scr->bottomY_; l = next(), ++ypos) {
        scr_overwrite_line(scr, ypos, l.b, l.sz, osz);
        osz = l.sz;
    }
    if (ypos < scr->bottomY_) {
        scr_overwrite_line(scr, ypos, NULL, 0, osz);
    }

    vdp_cursor_tab(scr->currY_, 0);
    int psz = 0;
    uint8_t* prefix = tb_prefix(buf, &psz);
    int i = 0;
    int pad = buf->x_ - scr->currX_;
    if (pad < 0) {
        pad = 0;
    }
    for (; i < scr->currX_; i++) {
        putch(prefix[i+pad]);
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    for (int j = 0; j < sz && i < scr->cols_; i++, j++) {
        putch(suffix[j]);
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

static void scroll_lines(screen* scr, text_buffer* buf, uint8_t ch) {
    if (scr->currY_ < scr->bottomY_-1) {
        line_itr next = tb_nline(buf, tb_ypos(buf));
        uint8_t ypos = scr->currY_+1;
        for (line l = next(); l.b != NULL && ypos < scr->bottomY_; l = next(), ++ypos) {
            scr_overwrite_line(scr, ypos, l.b, l.sz, l.osz);
        }
        scr->currY_++;
    } else {
        line_itr prev = tb_pline(buf);
        uint8_t ypos = scr->currY_;
        for (line l = prev(); ypos >= scr->topY_; l = prev(), --ypos) {
            scr_overwrite_line(scr, ypos, l.b, l.sz, l.osz);
        }
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

void cmd_show(screen* scr, text_buffer* buf) {
    uint8_t to_ch = tb_peek(buf);
    scroll_down_from_top(scr, buf, to_ch);
}

static void cmd_del_merge(screen* scr, text_buffer* buf) {
    if (!tb_del_merge(buf)) {
        return;
    }
    uint8_t ch = tb_peek(buf);
    scroll_up_from_top(scr, buf, ch);
}

void cmd_del(screen* scr, text_buffer* buf) {
    if (tb_eol(buf)) {
        if (tb_bol(buf)) {
            cmd_del_line(scr, buf);
        } else {
            cmd_del_merge(scr, buf);
        }
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

void cmd_del_line(screen* scr, text_buffer* buf) {
    if (!tb_del_line(buf)) {
        return;
    }
    uint8_t ch = tb_peek(buf);
    scroll_up_from_top(scr, buf, ch);
}

void cmd_left(screen* scr, text_buffer* buf) {
    if (tb_bol(buf)) {
        if (tb_ypos(buf) > 1) {
            cmd_up(scr, buf);
            cmd_end(scr, buf);
        }
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
        if (tb_ypos(buf) > 1) {
            cmd_up(scr, buf);
            cmd_end(scr, buf);
        }
        return;
    }
    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_prev(buf);
    const uint8_t deltaX = from_x - tb_xpos(buf);

    int sz = 0;
    uint8_t* suffix = tb_suffix(buf, &sz);
    scr_left(scr, from_ch, to_ch, deltaX, suffix, sz);
}

void cmd_right(screen* scr, text_buffer* buf) {
    if (tb_eol(buf)) {
        int ypos = tb_ypos(buf);
        cmd_down(scr, buf);
        if (ypos != tb_ypos(buf)) {
            cmd_home(scr, buf);
        }
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
    if (tb_eol(buf)) {
        int ypos = tb_ypos(buf);
        cmd_down(scr, buf);
        if (ypos != tb_ypos(buf)) {
            cmd_home(scr, buf);
        }
        return;
    }

    int from_x = tb_xpos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_w_next(buf);
    const uint8_t deltaX = tb_xpos(buf) - from_x;

    int sz = 0;
    uint8_t* prefix = tb_prefix(buf, &sz);
    scr_right(scr, from_ch, to_ch, deltaX, prefix, sz);
}

void cmd_up(screen* scr, text_buffer* buf) {
    int psz = 0;
    uint8_t* prefix = tb_prefix(buf, &psz);

    int ypos = tb_ypos(buf);
    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_up(buf);
    if (ypos == tb_ypos(buf)) {
        return;
    }

    if (scr->currY_ <= scr->topY_) {
        scroll_down_from_top(scr, buf, to_ch);
    } else {
        if (scr->currX_ >= scr->cols_-1) {
            scr_write_line(scr, scr->currY_, prefix, psz);
            if (tb_xpos(buf) >= scr->cols_) {
                psz = 0;
                prefix = tb_prefix(buf, &psz);
                int pad = (tb_xpos(buf)-1) - scr->currX_;
                scr_write_line(scr, scr->currY_-1, prefix+pad, psz-pad);
            }
        }
        int xpos = tb_xpos(buf)-1;
        if (xpos > scr->cols_-1) {
            xpos = scr->cols_-1;
        }
        scr_up(scr, from_ch, to_ch, xpos);
    }
}

void cmd_down(screen* scr, text_buffer* buf) {
    int psz = 0;
    uint8_t* prefix = tb_prefix(buf, &psz);

    uint8_t from_ch = tb_peek(buf);
    uint8_t to_ch = tb_down(buf);
    if (scr->currY_ == tb_ypos(buf)) {
        return;
    }
    if (scr->currY_ >= scr->bottomY_-1) {
        scroll_lines(scr, buf, to_ch);
    } else {
        if (scr->currX_ >= scr->cols_-1) {
            scr_write_line(scr, scr->currY_, prefix, psz);
            if (tb_xpos(buf) >= scr->cols_) {
                psz = 0;
                prefix = tb_prefix(buf, &psz);
                int pad = (tb_xpos(buf)-1) - scr->currX_;
                scr_write_line(scr, scr->currY_+1, prefix+pad, psz-pad);
            }
        }
        int xpos = tb_xpos(buf)-1;
        if (xpos > scr->cols_-1) {
            xpos = scr->cols_-1;
        }
        scr_down(scr, from_ch, to_ch, xpos);
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
