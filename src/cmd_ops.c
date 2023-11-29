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

#include "cmd_ops.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "text_buffer.h"
#include "screen.h"
#include "user_input.h"

#define SCR(ed) screen* scr = &ed->scr_
#define UI(ed) user_input* ui = &ed->ui_
#define TB(ed) text_buffer* tb = &ed->buf_

static void refresh_screen(screen* scr, text_buffer* tb) {
    uint8_t currY = scr->currY_;
    uint8_t currX = scr->currX_;

    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    while (tb_ypos(&cp) > 1 &&  scr->currY_ > scr->topY_) {
        tb_up(&cp);
        scr->currY_--;
    }
    scr_clear(scr);

    scr->currX_ = 0;
    do {
        int ypos = scr->currY_;
        int sz = 0;
        uint8_t* suffix = tb_suffix(&cp, &sz);
        scr_overwrite_line(scr, ypos, suffix, sz, sz);
        scr->currY_++;
    } while (tb_down(&cp) != 0 && scr->currY_ < scr->bottomY_);

    vdp_cursor_tab(currY, currX);
    scr->currY_ = currY;
    scr->currX_ = currX;
}

static bool update_fname(screen* scr, user_input* ui, text_buffer* tb, const char* prefill) {
    uint8_t* fname;
    uint8_t sz;
    RESPONSE res = ui_text(ui, scr, "File name: ", prefill, &fname, &sz);
    if (res == CANCEL_OPT) {
        return false;
    } else if (res == YES_OPT) {
        strncpy(tb->fname_, fname, sz);
        tb->fname_[sz] = 0;
        return true;
    }
    return false;
}

static bool save_file(text_buffer* tb) {
    if (!tb_valid_file(tb)) {
        return false;
    }

    uint8_t fh = mos_fopen(tb->fname_, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;;
    }

    uint8_t* prefix = NULL;
    uint8_t* suffix = NULL;
    int psz = 0;
    int ssz = 0;
    tb_content(tb, &prefix, &psz, &suffix, &ssz);

    if (prefix != NULL && psz > 0) {
        mos_fwrite(fh, (char*) prefix, psz);
    }
    if (suffix != NULL && ssz > 0) {
        mos_fwrite(fh, (char*) suffix, ssz);
    }

    mos_fclose(fh);
    tb_saved(tb);

    return true;
}

bool cmd_save(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (!tb_changed(tb)) {
        return true;
    }

    if (!tb_valid_file(tb) && !update_fname(scr, ui, tb, NULL)) {
        return false;
    }

    return save_file(tb);
}


void cmd_save_as(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (update_fname(scr, ui, tb, tb->fname_)) {
        save_file(tb);
    }
}


bool cmd_quit(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (!tb_changed(tb)) {
        return true;
    }

    RESPONSE res = ui_dialog(ui, scr, "Save before quit?");
    if (res == NO_OPT) {
        return true;
    }
    if (res == CANCEL_OPT) {
        return false;
    }

    return cmd_save(ed);
}

void cmd_color_picker(editor* ed) {
    SCR(ed);
    UI(ed);

    RESPONSE ret = ui_color_picker(ui, scr);
    if (ret == YES_OPT) {
        TB(ed);
        uint8_t ch = tb_peek(tb);
        refresh_screen(scr, tb);
        scr_show_cursor_ch(scr, ch);
    }
}

void cmd_putc(editor* ed, key k) {
    TB(ed);
    SCR(ed);

    if (k.key == '\t') {
        // Convert tab to spaces because it is too damn hard to get it working correctly with line scrolling.
        k.key = ' ';
        const uint8_t spaces = scr->tab_size_ - ((tb_xpos(tb)-1) % scr->tab_size_);
        for (uint8_t i = 0; i < spaces; i++) {
            cmd_putc(ed, k);
        }
        return;
    }

    tb_put(tb, k.key);
    int psz = 0;
    uint8_t* prefix = tb_prefix(tb, &psz);
    int ssz = 0;
    uint8_t* suffix = tb_suffix(tb, &ssz);
    scr_putc(scr, k.key, prefix, psz, suffix, ssz);
}

static void scr_write_padded(editor* ed) {
    TB(ed);
    SCR(ed);

    vdp_cursor_tab(scr->currY_, 0);
    int psz = 0;
    uint8_t* prefix = tb_prefix(tb, &psz);
    int i = 0;
    int pad = tb->x_ - scr->currX_;
    if (pad < 0) {
        pad = 0;
    }
    for (; i < scr->currX_; i++) {
        putch(prefix[i+pad]);
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(tb, &sz);
    for (int j = 0; j < sz && i < scr->cols_; i++, j++) {
        putch(suffix[j]);
    }
}

static void scroll_from_top(editor* ed, uint8_t ch, const bool osz_is_last) {
    TB(ed);
    SCR(ed);

    vdp_cursor_tab(scr->currY_+1, 0);
    scr_hide_cursor_ch(scr, ch);
    vdp_cursor_tab(scr->currY_, 0);
    line_itr next = tb_nline(tb, tb_ypos(tb));
    uint8_t ypos = scr->currY_;
    int osz = 255;
    for (line l = next(); l.b != NULL && ypos < scr->bottomY_; l = next(), ++ypos) {
        if (!osz_is_last) {
            osz = l.osz;
        }
        scr_overwrite_line(scr, ypos, l.b, l.sz, osz);
        if (osz_is_last) {
            osz = l.sz;
        }
    }
    if (ypos < scr->bottomY_) {
        scr_write_line(scr, ypos, NULL, 0);
    }
    scr_write_padded(ed);
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);

}

static void scroll_up_from_top(editor* ed, uint8_t ch) {
    scroll_from_top(ed, ch, false);
}

static void scroll_down_from_top(editor* ed, uint8_t ch) {
    scroll_from_top(ed, ch, true);
}


static void scroll_lines(editor* ed, uint8_t ch) {
    TB(ed);
    SCR(ed);

    if (scr->currY_ < scr->bottomY_-1) {
        line_itr next = tb_nline(tb, tb_ypos(tb));
        uint8_t ypos = scr->currY_+1;
        for (line l = next(); l.b != NULL && ypos < scr->bottomY_; l = next(), ++ypos) {
            scr_overwrite_line(scr, ypos, l.b, l.sz, l.osz);
        }
        scr->currY_++;
    } else {
        line_itr prev = tb_pline(tb);
        uint8_t ypos = scr->currY_;
        for (line l = prev(); ypos >= scr->topY_; l = prev(), --ypos) {
            scr_overwrite_line(scr, ypos, l.b, l.sz, l.osz);
        }
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

void cmd_show(editor* ed) {
    TB(ed);

    const uint8_t to_ch = tb_peek(tb);
    scroll_down_from_top(ed, to_ch);
}

static void cmd_del_merge(editor* ed) {
    TB(ed);

    if (!tb_del_merge(tb)) {
        return;
    }
    const uint8_t ch = tb_peek(tb);
    scroll_down_from_top(ed, ch);
}

void cmd_del(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_eol(tb)) {
        if (tb_bol(tb)) {
            cmd_del_line(ed);
        } else {
            cmd_del_merge(ed);
        }
        return;
    }
    if (!tb_del(tb)) {
        return;
    }
    int sz = 0;
    uint8_t* suffix = tb_suffix(tb, &sz);
    scr_del(scr, suffix, sz);
}

static void cmd_bksp_merge(editor* ed) {
    TB(ed);
    SCR(ed);

    if (!tb_bksp_merge(tb)) {
        return;
    }
    uint8_t ch = tb_peek(tb);
    if (scr->currY_ > scr->topY_) {
        scr->currY_--;
    }
    if (tb->x_ > scr->cols_-1) {
        scr->currX_ = scr->cols_-1;
    } else {
        scr->currX_ = tb->x_;
    }
    scroll_down_from_top(ed, ch);
}

void cmd_bksp(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        if (tb_ypos(tb) > 1) {
            cmd_bksp_merge(ed);
        }
        return;
    }

    int sz = 0;
    uint8_t* suffix = tb_suffix(tb, &sz);

    if (!tb_bksp(tb)) {
        return;
    }
    scr_bksp(scr, suffix, sz);
}

void cmd_newl(editor* ed) {
    TB(ed);
    SCR(ed);

    uint8_t ch = tb_peek(tb);
    int sz = 0;
    uint8_t* prefix = tb_prefix(tb, &sz);
    if (!tb_newline(tb)) {
        return;
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_write_line(scr, scr->currY_, prefix, sz);
    scr->currX_ = 0;
    scroll_lines(ed, ch);
}

void cmd_del_line(editor* ed) {
    TB(ed);
    SCR(ed);

    if (!tb_del_line(tb)) {
        return;
    }
    scr->currX_ = 0;
    uint8_t ch = tb_peek(tb);
    scroll_down_from_top(ed, ch);
}

void cmd_left(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        if (tb_ypos(tb) > 1) {
            cmd_up(ed);
            cmd_end(ed);
        }
        return;
    }
    uint8_t from_ch = tb_peek(tb);
    uint8_t to_ch = tb_prev(tb);

    int sz = 0;
    uint8_t* suffix = tb_suffix(tb, &sz);
    scr_left(scr, from_ch, to_ch, 1, suffix, sz);
}

void cmd_w_left(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        if (tb_ypos(tb) > 1) {
            cmd_up(ed);
            cmd_end(ed);
        }
        return;
    }

    const int from_x = tb_xpos(tb);
    const uint8_t from_ch = tb_peek(tb);
    const uint8_t to_ch = tb_w_prev(tb);
    const uint8_t deltaX = from_x - tb_xpos(tb);

    int sz = 0;
    uint8_t* suffix = tb_suffix(tb, &sz);
    scr_left(scr, from_ch, to_ch, deltaX, suffix, sz);
}

void cmd_right(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_eol(tb)) {
        int ypos = tb_ypos(tb);
        cmd_down(ed);
        if (ypos != tb_ypos(tb)) {
            cmd_home(ed);
        }
        return;
    }

    uint8_t from_ch = tb_peek(tb);
    if (from_ch == 0 ) {
        return;
    }

    const uint8_t to_ch = tb_next(tb);
    int sz = 0;
    uint8_t* prefix = tb_prefix(tb, &sz);
    scr_right(scr, from_ch, to_ch, 1, prefix, sz);
}

void cmd_w_right(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_eol(tb)) {
        int ypos = tb_ypos(tb);
        cmd_down(ed);
        if (ypos != tb_ypos(tb)) {
            cmd_home(ed);
        }
        return;
    }

    const int from_x = tb_xpos(tb);
    const uint8_t from_ch = tb_peek(tb);
    const uint8_t to_ch = tb_w_next(tb);
    const uint8_t deltaX = tb_xpos(tb) - from_x;

    int sz = 0;
    uint8_t* prefix = tb_prefix(tb, &sz);
    scr_right(scr, from_ch, to_ch, deltaX, prefix, sz);
}

void cmd_up(editor* ed) {
    TB(ed);
    SCR(ed);

    int psz = 0;
    uint8_t* prefix = tb_prefix(tb, &psz);

    int ypos = tb_ypos(tb);
    uint8_t from_ch = tb_peek(tb);
    uint8_t to_ch = tb_up(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }

    if (scr->currY_ <= scr->topY_) {
        scroll_up_from_top(ed, to_ch);
        return;
    }

    if (scr->currX_ >= scr->cols_-1) {
        scr_write_line(scr, scr->currY_, prefix, psz);
        if (tb_xpos(tb) >= scr->cols_) {
            psz = 0;
            prefix = tb_prefix(tb, &psz);
            int pad = (tb_xpos(tb)-1) - scr->currX_;
            scr_write_line(scr, scr->currY_-1, prefix+pad, psz-pad);
        }
    }

    int xpos = tb_xpos(tb)-1;
    if (xpos > scr->cols_-1) {
        xpos = scr->cols_-1;
    }
    scr_up(scr, from_ch, to_ch, xpos);
}

void cmd_down(editor* ed) {
    TB(ed);
    SCR(ed);

    int psz = 0;
    uint8_t* prefix = tb_prefix(tb, &psz);

    uint8_t from_ch = tb_peek(tb);
    uint8_t to_ch = tb_down(tb);
    if (scr->currY_ == tb_ypos(tb)) {
        return;
    }
    if (scr->currY_ >= scr->bottomY_-1) {
        scroll_lines(ed, to_ch);
        return;
    }
    if (scr->currX_ >= scr->cols_-1) {
        scr_write_line(scr, scr->currY_, prefix, psz);
        if (tb_xpos(tb) >= scr->cols_) {
            psz = 0;
            prefix = tb_prefix(tb, &psz);
            int pad = (tb_xpos(tb)-1) - scr->currX_;
            scr_write_line(scr, scr->currY_+1, prefix+pad, psz-pad);
        }
    }

    int xpos = tb_xpos(tb)-1;
    if (xpos > scr->cols_-1) {
        xpos = scr->cols_-1;
    }
    scr_down(scr, from_ch, to_ch, xpos);
}

void cmd_home(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        return;
    }

    uint8_t from_ch = tb_peek(tb);
    uint8_t to_ch = tb_home(tb);
    if (to_ch != 0) {
        int sz = 0;
        uint8_t* suffix = tb_suffix(tb, &sz);
        scr_home(scr, from_ch, tb_peek(tb), suffix, sz);
    }
}

void cmd_end(editor* ed) {
    TB(ed);
    SCR(ed);

    const int from_x = tb_xpos(tb);
    uint8_t from_ch = tb_peek(tb);
    uint8_t to_ch = tb_end(tb);
    const int deltaX = tb_xpos(tb) - from_x;
    if (deltaX > 0) {
        int sz = 0;
        uint8_t* prefix = tb_prefix(tb, &sz);
        scr_end(scr, from_ch, to_ch, deltaX, prefix, sz);
    }
}

void cmd_page_up(editor* ed) {
    TB(ed);
    SCR(ed);

    const int curr = scr->currY_ - scr->topY_;
    const int page = scr->bottomY_ - scr->topY_+1;
    int remaining = tb_ypos(tb)-1 - curr;

    for (int i = 0; i < page && remaining > 0; i++, remaining--) {
        tb_up(tb);
    }
    uint8_t ch = tb_peek(tb);
    scr->currX_ = tb_xpos(tb) < scr->cols_ ? tb_xpos(tb)-1 : scr->cols_-1;
    refresh_screen(scr, tb);
    scr_show_cursor_ch(scr, ch);
}

void cmd_page_down(editor* ed) {
    TB(ed);
    SCR(ed);

    const int curr = scr->bottomY_ - scr->currY_;
    const int page = scr->bottomY_ - scr->topY_;
    int remaining = tb_ymax(tb) - tb_ypos(tb) - curr + 1;

    for (int i = 0; i < page && remaining > 0; i++, remaining--) {
        tb_down(tb);
    }
    uint8_t ch = tb_peek(tb);
    scr->currX_ = tb_xpos(tb) < scr->cols_ ? tb_xpos(tb)-1 : scr->cols_-1;
    refresh_screen(scr, tb);
    scr_show_cursor_ch(scr, ch);
}
