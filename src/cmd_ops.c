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

static void fill_screen(screen* scr, text_buffer* tb) {
    scr_clear_textarea(scr, scr->topY_, scr->bottomY_);

    char tpos = tb_ypos(tb);
    for (char ypos = scr->topY_; ypos < scr->bottomY_; ypos++) {
        int sz = 0;
        char* suffix = tb_suffix(tb, &sz);
        scr_write_line(scr, ypos, suffix, sz);

        tb_down(tb);
        const int npos = tb_ypos(tb);
        if (npos == tpos) {
            break;
        }
        tpos = npos;
    }
}


static void refresh_screen(screen* scr, text_buffer* tb) {
    char currY = scr->currY_;
    char currX = scr->currX_;

    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    while (tb_ypos(&cp) > 1 &&  scr->currY_ > scr->topY_) {
        tb_up(&cp);
        scr->currY_--;
    }
    fill_screen(scr, &cp);

    vdp_cursor_tab(currY, currX);
    scr->currY_ = currY;
    scr->currX_ = currX;
}

static bool update_fname(screen* scr, user_input* ui, text_buffer* tb, char* prefill) {
    char* fname;
    int sz;
    RESPONSE res = ui_text(ui, scr, "File name: ", prefill, &fname, &sz);
    if (res == CANCEL_OPT) {
        return false;
    } else if (res == YES_OPT) {
        strncpy(tb->fname_, fname, sz);
        tb->fname_[(int)sz] = 0;
        return true;
    }
    return false;
}

static bool save_file(text_buffer* tb) {
    if (!tb_valid_file(tb)) {
        return false;
    }

    char fh = mos_fopen(tb->fname_, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;;
    }

    char* prefix = NULL;
    char* suffix = NULL;
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
        char ch = tb_peek(tb);
        scr_clear(scr);
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
        const char spaces = scr->tab_size_ - ((tb_xpos(tb)-1) % scr->tab_size_);
        for (char i = 0; i < spaces; i++) {
            cmd_putc(ed, k);
        }
        return;
    }

    tb_put(tb, k.key);
    split_line ln = tb_curr_line(tb);
    scr_putc(scr, k.key, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);
}

static inline void reset_viewport() {
    putch(26);
}

static void define_viewport(char left, char bottom, char right, char top) {
    static char viewport[5] = {28, 0, 0, 0, 0};
    viewport[1] = left;
    viewport[2] = bottom;
    viewport[3] = right;
    viewport[4] = top;
    VDP_PUTS(viewport);
}

static void scroll_down(
        screen* scr, char topY, char bottomY, char* line, int sz, char ch) {
    static const char down[] = {23, 7, 0, 2, 8};
    define_viewport(0, bottomY, scr->cols_, topY);
    VDP_PUTS(down);
    reset_viewport();
    scr_write_line(scr, scr->currY_, line, sz);
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

static void scroll_up(
        screen* scr, char topY, char bottomY, char* line, int sz, char ch) {
    static const char up[] = {23, 7, 0, 3, 8};
    define_viewport(0, bottomY, scr->cols_, topY);
    VDP_PUTS(up);
    reset_viewport();
    scr_write_line(scr, scr->currY_, line, sz);
    vdp_cursor_tab(scr->currY_, scr->currX_);
    scr_show_cursor_ch(scr, ch);
}

static void region_up(screen* scr, text_buffer* tb, char ch) {
    int sz = 0;
    char* line = tb_suffix(tb, &sz);
    scroll_up(scr, scr->currY_, scr->bottomY_-1, line, sz, ch);

    int diff = scr->bottomY_ - scr->currY_ - 1;
    int last = 0;
    int curr = 0;
    while (diff-- > 0) {
        curr = tb_down(tb);
        if (curr == last) {
            scr_write_line(scr, scr->bottomY_-1, NULL, 0);
            return;
        }
    }
    line = tb_suffix(tb, &sz);
    scr_overwrite_line(scr, scr->bottomY_-1, line, sz, 255);
}

void cmd_show(editor* ed) {
    TB(ed);
    SCR(ed);

    text_buffer cb;
    tb_copy(&cb, tb);
    fill_screen(scr, &cb);
    vdp_cursor_tab(scr->currY_, scr->currX_);

    const char to_ch = tb_peek(tb);
    scr_show_cursor_ch(scr, to_ch);
}

static void cmd_del_merge(editor* ed) {
    TB(ed);
    if (!tb_del_merge(tb)) {
        return;
    }
    SCR(ed);

    const char ch = tb_peek(tb);
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    region_up(scr, &cp, ch);
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
    char* suffix = tb_suffix(tb, &sz);
    scr_del(scr, suffix, sz);
}

static void cmd_bksp_merge(editor* ed) {
    TB(ed);
    SCR(ed);

    if (!tb_bksp_merge(tb)) {
        return;
    }
    if (scr->currY_ > scr->topY_) {
        scr->currY_--;
    }
    if (tb->x_ > scr->cols_-1) {
        scr->currX_ = scr->cols_-1;
    } else {
        scr->currX_ = tb->x_;
    }

    char ch = tb_peek(tb);
    text_buffer cp;

    tb_copy(&cp, tb);
    tb_home(&cp);
    region_up(scr, &cp, ch);
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
    char* suffix = tb_suffix(tb, &sz);

    if (!tb_bksp(tb)) {
        return;
    }
    scr_bksp(scr, suffix, sz);
}

void cmd_newl(editor* ed) {
    TB(ed);
    SCR(ed);

    char ch = tb_peek(tb);
    split_line ln = tb_curr_line(tb);

    if (!tb_newline(tb)) {
        return;
    }
    scr_write_line(scr, scr->currY_, ln.prefix_, ln.psz_);

    scr->currX_ = 0;
    if  (scr->currY_ < scr->bottomY_-1) {
        scr->currY_++;
        scroll_down(scr, scr->currY_, scr->bottomY_-1, ln.suffix_, ln.ssz_, ch);
    } else {
        scroll_up(scr, scr->topY_, scr->bottomY_-1, ln.suffix_, ln.ssz_, ch);
    }
}

void cmd_del_line(editor* ed) {
    TB(ed);
    SCR(ed);

    if (!tb_del_line(tb)) {
        return;
    }
    scr->currX_ = 0;

    const char ch = tb_peek(tb);
    text_buffer cp;

    tb_copy(&cp, tb);
    tb_home(&cp);
    region_up(scr, &cp, ch);
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
    char from_ch = tb_peek(tb);
    char to_ch = tb_prev(tb);

    int sz = 0;
    char* suffix = tb_suffix(tb, &sz);
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
    const char from_ch = tb_peek(tb);
    const char to_ch = tb_w_prev(tb, from_ch);
    const char deltaX = from_x - tb_xpos(tb);

    int sz = 0;
    char* suffix = tb_suffix(tb, &sz);
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

    char from_ch = tb_peek(tb);
    if (from_ch == 0 ) {
        return;
    }

    const char to_ch = tb_next(tb);
    int sz = 0;
    char* prefix = tb_prefix(tb, &sz);
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
    const char from_ch = tb_peek(tb);
    const char to_ch = tb_w_next(tb, from_ch);
    const char deltaX = tb_xpos(tb) - from_x;

    int sz = 0;
    char* prefix = tb_prefix(tb, &sz);
    scr_right(scr, from_ch, to_ch, deltaX, prefix, sz);
}

void cmd_up(editor* ed) {
    TB(ed);
    SCR(ed);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);

    int ypos = tb_ypos(tb);
    char from_ch = tb_peek(tb);
    char to_ch = tb_up(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }

    if (scr->currY_ == scr->topY_) {
        scr_hide_cursor_ch(scr, from_ch);
        scr->currX_ = 0;
        vdp_cursor_tab(scr->currY_, scr->currX_);

        tb_home(tb);
        to_ch = tb_peek(tb);

        char* suffix = tb_suffix(tb, &psz);
        scroll_down(scr, scr->topY_, scr->bottomY_-1, suffix, psz, to_ch);
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

    int ypos = tb_ypos(tb);
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    char from_ch = tb_peek(tb);
    char to_ch = tb_down(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }
    if (scr->currY_ >= scr->bottomY_-1) {
        scr_hide_cursor_ch(scr, from_ch);
        scr->currX_ = tb_xpos(tb)-1;
        if (scr->currX_ > scr->cols_-1) {
            scr->currX_ = scr->cols_-1;
        }

        text_buffer cp;
        tb_copy(&cp, tb);
        tb_home(&cp);

        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        scroll_up(scr, scr->topY_, scr->bottomY_-1, line, sz, to_ch);
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

    char from_ch = tb_peek(tb);
    char to_ch = tb_home(tb);
    if (to_ch != 0) {
        int sz = 0;
        char* suffix = tb_suffix(tb, &sz);
        scr_home(scr, from_ch, tb_peek(tb), suffix, sz);
    }
}

void cmd_end(editor* ed) {
    TB(ed);
    SCR(ed);

    const int from_x = tb_xpos(tb);
    char from_ch = tb_peek(tb);
    char to_ch = tb_end(tb);
    const int deltaX = tb_xpos(tb) - from_x;
    if (deltaX > 0) {
        int sz = 0;
        char* prefix = tb_prefix(tb, &sz);
        scr_end(scr, from_ch, to_ch, deltaX, prefix, sz);
    }
}

void cmd_page_up(editor* ed) {
    TB(ed);
    SCR(ed);

    const int curr = scr->currY_ - scr->topY_;
    const int page = scr->bottomY_ - scr->topY_+1;
    int remaining = tb_ypos(tb)-1 - curr;

    if (remaining <= 0) {
        remaining = tb_ypos(tb)-1;
        scr->currY_ = scr->topY_;
    }
    for (int i = 0; i < page && remaining > 0; i++, remaining--) {
        tb_up(tb);
    }

    char ch = tb_peek(tb);
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

    if (remaining <= 0) {
        remaining = tb_ymax(tb) - tb_ypos(tb);
        scr->currY_ = scr->bottomY_-1;
    }
    for (int i = 0; i < page && remaining > 0; i++, remaining--) {
        tb_down(tb);
    }
    char ch = tb_peek(tb);
    scr->currX_ = tb_xpos(tb) < scr->cols_ ? tb_xpos(tb)-1 : scr->cols_-1;
    refresh_screen(scr, tb);
    scr_show_cursor_ch(scr, ch);
}

void cmd_goto(editor* ed) {
    SCR(ed);
    UI(ed);
    TB(ed);

    int line = 0;
    RESPONSE goto_line = ui_goto(ui, scr, &line);
    if (goto_line != YES_OPT) {
        return;
    }

    const int ypos = tb_ypos(tb);
    if (ypos == line) {
        return;
    }

    int diff = 0;
    scr_hide_cursor_ch(scr, tb_peek(tb));
    if (line < ypos) {
        for (; line < ypos; line++) {
            diff--;
            tb_up(tb);
            if (tb_ypos(tb) == 1) {
                break;
            }
        }
    } else {
        int curr = tb_ypos(tb);
        for (; ypos < line; line--) {
            tb_down(tb);
            const int nyp = tb_ypos(tb);
            if (nyp == curr) {
                break;
            }
            curr = nyp;
            diff++;
        }
    }

    diff = ((int)scr->currY_) + diff;
    if (diff < (int)scr->topY_) {
        scr->currY_ = scr->topY_;
    } else if (diff >= (int) scr->bottomY_) {
        scr->currY_ = scr->bottomY_-1;
    } else {
        scr->currY_ = diff;
    }
    vdp_cursor_tab(scr->currY_, scr->currX_);

    scr->currX_ = tb_xpos(tb) < scr->cols_ ? tb_xpos(tb)-1 : scr->cols_-1;
    refresh_screen(scr, tb);
    scr_show_cursor_ch(scr, tb_peek(tb));
}

