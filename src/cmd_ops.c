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

#include <stddef.h>

#include "config.h"
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

    scr->currY_ = currY;
    scr->currX_ = currX;
    scr_sync_cursor(scr);
}

// The horizontal origin is screen-wide, so a scroll invalidates every visible
// row, not just the one the cursor is on. Only the controller can repaint them:
// the view has no way to walk the document.
// Paints `count` screen columns starting at `sx`, one cell per visible row.
// Used after a VDP region scroll has shifted the text area sideways: only the
// newly exposed columns are unknown, the rest moved with the hardware.
static void fill_columns(screen* scr, text_buffer* tb, char sx, int count) {
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_home(&cp);
    int up = scr->currY_ - scr->topY_;
    while (up-- > 0 && tb_ypos(&cp) > 1) {
        tb_up(&cp);
    }

    int tpos = tb_ypos(&cp);
    for (char ypos = scr->topY_; ypos < scr->bottomY_; ypos++) {
        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        for (int i = 0; i < count; i++) {
            const char g = scr_glyph_at(scr, line, sz, scr->originX_ + sx + i);
            scr_put_at(scr, (char)(sx + i), ypos, g);
        }

        tb_down(&cp);
        const int npos = tb_ypos(&cp);
        if (npos == tpos) {
            break;
        }
        tpos = npos;
    }
}

// The horizontal origin is screen-wide, so a scroll invalidates every visible
// row. For a short hop the VDP can shift the whole text area itself and only
// the exposed columns need drawing; past that a full repaint is cheaper.
// `edited` says whether the current row's text changed. The region scroll moves
// pixels, which only reproduces the document while the text is unchanged, so an
// edit that also scrolls must have its row redrawn from the buffer afterwards.
static void resync_after_scroll(screen* scr, text_buffer* tb, char to_ch,
                                int delta, bool edited) {
    if (delta == 0) {
        return;
    }

    if (delta > SCR_MAX_HSCROLL || delta < -SCR_MAX_HSCROLL) {
        refresh_screen(scr, tb);   // reads the document, so already correct
    } else {
        scr_scroll_h(scr, delta);
        if (delta > 0) {
            fill_columns(scr, tb, (char)(scr->cols_ - delta), delta);
        } else {
            fill_columns(scr, tb, 0, -delta);
        }
        if (edited) {
            split_line ln = tb_curr_line(tb);
            scr_paint_row(scr, scr->currY_, ln.prefix_, ln.psz_,
                          ln.suffix_, ln.ssz_);
        }
    }
    scr_sync_cursor(scr);
    scr_show_cursor_ch(scr, to_ch);
}

static bool update_fname(screen* scr, user_input* ui, text_buffer* tb, char* prefill) {
    char* fname;
    int sz;
    RESPONSE res = ui_text(ui, scr, "File name: ", prefill, &fname, &sz);
    if (res == CANCEL_OPT) {
        return false;
    } else if (res == YES_OPT) {
        tb_set_fname(tb, fname, sz);
        return true;
    }
    return false;
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

    return tb_save(tb);
}


void cmd_save_as(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    if (update_fname(scr, ui, tb, tb_fname(tb))) {
        tb_save(tb);
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

void cmd_open(editor* ed) {
    TB(ed);
    SCR(ed);
    UI(ed);

    // Same bargain as quitting: the document on screen is about to go, so the
    // user gets the same chance to keep it, and answering neither way calls the
    // whole thing off.
    if (tb_changed(tb)) {
        RESPONSE res = ui_dialog(ui, scr, "Save before opening?");
        if (res == CANCEL_OPT) {
            return;
        }
        if (res == YES_OPT && !cmd_save(ed)) {
            return;   // the save was cancelled or failed; keep the document
        }
    }

    char* fname;
    int sz;
    // Prefilled with the current name so the prompt shows the shape of what it
    // wants, and so opening a mistyped name again is a small edit.
    if (ui_text(ui, scr, "Open file: ", tb_fname(tb), &fname, &sz) != YES_OPT) {
        return;
    }

    switch (tb_open(tb, fname, sz)) {
        case TB_TOO_LARGE:
            ui_message(ui, scr, "File too large");
            return;
        case TB_NO_FILE:
            ui_message(ui, scr, "Cannot open file");
            return;
        case TB_OK:
            break;
    }

    // scr_clear winds the cursor and the horizontal origin back to the start
    // as well as repainting the banner, which is exactly the reset a whole new
    // document needs.
    scr_clear(scr);
    cmd_show(ed);
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

        // Write the choice down. Settings live in the file now, and a scheme
        // that vanished on exit was the wart this was meant to fix. Only the
        // colours are set here: everything left unset is copied through
        // untouched, so picking a colour cannot rewrite or invent a tab setting
        // the user never asked to change.
        config cfg;
        cfg_defaults(&cfg);
        cfg.fg = scr_fg(scr);
        cfg.bg = scr_bg(scr);
        cfg_update(&cfg, CFG_PATH);
    }
}

void cmd_putc(editor* ed, key k) {
    TB(ed);
    SCR(ed);

    if (!tb_put(tb, k.key)) {
        return;
    }
    split_line ln = tb_curr_line(tb);
    const int moved = scr_putc(scr, k.key, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);
    if (moved != 0) {
        resync_after_scroll(scr, tb, tb_peek(tb), moved, true);
    }
}

static void region_up(screen* scr, text_buffer* tb, char ch) {
    int sz = 0;
    char* line = tb_suffix(tb, &sz);
    scr_scroll_up(scr, scr->currY_, scr->bottomY_-1, line, sz, ch);

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
    scr_sync_cursor(scr);

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
    int bsz = 0;
    char* bprefix = tb_prefix(tb, &bsz);
    scr_place_cursor(scr, bprefix, bsz);

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

    if (!tb_bksp(tb)) {
        return;
    }
    split_line ln = tb_curr_line(tb);
    const int moved = scr_bksp(scr, ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_);
    if (moved != 0) {
        resync_after_scroll(scr, tb, tb_peek(tb), moved, true);
    }
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

    scr_place_cursor(scr, NULL, 0);
    if  (scr->currY_ < scr->bottomY_-1) {
        scr->currY_++;
        scr_scroll_down(scr, scr->currY_, scr->bottomY_-1, ln.suffix_, ln.ssz_, ch);
    } else {
        scr_scroll_up(scr, scr->topY_, scr->bottomY_-1, ln.suffix_, ln.ssz_, ch);
    }
}

void cmd_del_line(editor* ed) {
    TB(ed);
    SCR(ed);

    if (!tb_del_line(tb)) {
        return;
    }
    scr_place_cursor(scr, NULL, 0);

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

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, to_ch,
                                      ln.prefix_, ln.psz_);
    if (moved != 0) {
        resync_after_scroll(scr, tb, to_ch, moved, false);
    }
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

    const char from_ch = tb_peek(tb);
    const char to_ch = tb_w_prev(tb, from_ch);

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(scr, tb, to_ch, moved, false);
    }
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

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(scr, tb, to_ch, moved, false);
    }
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

    const char from_ch = tb_peek(tb);
    const char to_ch = tb_w_next(tb, from_ch);

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(scr, tb, to_ch, moved, false);
    }
}

void cmd_up(editor* ed) {
    TB(ed);
    SCR(ed);

    // Remember the column, not the byte offset: a tab is one byte but several
    // columns, so carrying x_ across would slide the cursor sideways.
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    const int want_col = scr_column_of(scr, prefix, psz);

    const int ypos = tb_ypos(tb);
    const char from_ch = tb_peek(tb);
    tb_up(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }

    // Land on the byte of the new line that renders at that column.
    tb_home(tb);
    int lsz = 0;
    char* row = tb_suffix(tb, &lsz);
    const char to_ch = tb_goto_offset(tb, scr_byte_at(scr, row, lsz, want_col));

    if (scr->currY_ == scr->topY_) {
        scr_hide_cursor_ch(scr, from_ch);
        split_line top = tb_curr_line(tb);
        (void) scr_place_cursor(scr, top.prefix_, top.psz_);

        text_buffer cp;
        tb_copy(&cp, tb);
        tb_home(&cp);
        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        scr_scroll_down(scr, scr->topY_, scr->bottomY_-1, line, sz, to_ch);
        return;
    }

    split_line ln = tb_curr_line(tb);
    const int moved = scr_up(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(scr, tb, to_ch, moved, false);
    }
}

void cmd_down(editor* ed) {
    TB(ed);
    SCR(ed);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    const int want_col = scr_column_of(scr, prefix, psz);

    const int ypos = tb_ypos(tb);
    const char from_ch = tb_peek(tb);
    tb_down(tb);
    if (ypos == tb_ypos(tb)) {
        return;
    }

    tb_home(tb);
    int lsz = 0;
    char* row = tb_suffix(tb, &lsz);
    const char to_ch = tb_goto_offset(tb, scr_byte_at(scr, row, lsz, want_col));

    if (scr->currY_ >= scr->bottomY_-1) {
        scr_hide_cursor_ch(scr, from_ch);
        split_line bot = tb_curr_line(tb);
        (void) scr_place_cursor(scr, bot.prefix_, bot.psz_);

        text_buffer cp;
        tb_copy(&cp, tb);
        tb_home(&cp);
        int sz = 0;
        char* line = tb_suffix(&cp, &sz);
        scr_scroll_up(scr, scr->topY_, scr->bottomY_-1, line, sz, to_ch);
        return;
    }

    split_line ln = tb_curr_line(tb);
    const int moved = scr_down(scr, from_ch, to_ch, ln.prefix_, ln.psz_);
    if (moved != 0) {
            resync_after_scroll(scr, tb, to_ch, moved, false);
    }
}

void cmd_home(editor* ed) {
    TB(ed);
    SCR(ed);

    if (tb_bol(tb)) {
        return;
    }

    char from_ch = tb_peek(tb);
    tb_home(tb);

    split_line ln = tb_curr_line(tb);
    const int moved = scr_move_cursor(scr, from_ch, tb_peek(tb),
                                      ln.prefix_, ln.psz_);
    if (moved != 0) {
        resync_after_scroll(scr, tb, tb_peek(tb), moved, false);
    }
}

void cmd_end(editor* ed) {
    TB(ed);
    SCR(ed);

    const int from_x = tb_xpos(tb);
    char from_ch = tb_peek(tb);
    char to_ch = tb_end(tb);
    if (tb_xpos(tb) != from_x) {
        split_line ln = tb_curr_line(tb);
        const int moved = scr_move_cursor(scr, from_ch, to_ch,
                                          ln.prefix_, ln.psz_);
        if (moved != 0) {
            resync_after_scroll(scr, tb, to_ch, moved, false);
        }
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
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
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
    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
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
    scr_sync_cursor(scr);

    int psz = 0;
    char* prefix = tb_prefix(tb, &psz);
    scr_place_cursor(scr, prefix, psz);
    refresh_screen(scr, tb);
    scr_show_cursor_ch(scr, tb_peek(tb));
}

