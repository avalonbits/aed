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

#include "editor.h"

#include <agon/vdp.h>
#include <agon/mos.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cmd_ops.h"
#include "input.h"
#include "config.h"

#define DEFAULT_CURSOR 32

editor* ed_init(editor* ed, int mem_kb, const char* fname) {
    screen* scr = scr_init(&ed->scr_, DEFAULT_CURSOR);

    // Settings are read once at startup. The setters clamp or reject out of
    // range values, so a bad number in the file falls back rather than
    // rejecting the file -- there is nowhere useful to report an error to.
    //
    // On first run there is no file. Write one holding what AED is starting
    // with, including the colours it just measured off the Agon, so the user
    // has something to edit instead of a format to guess at.
    config cfg;
    cfg_defaults(&cfg);
    if (cfg_load(&cfg, CFG_PATH)) {
        if (cfg.tab_size >= 0) {
            scr_set_tab_size(scr, (char) cfg.tab_size);
        }
        // Each colour applies on its own: a file that sets only fg keeps the
        // measured bg, the same way an unset tab keeps the default.
        if (cfg.fg >= 0 || cfg.bg >= 0) {
            const char fg = cfg.fg >= 0 ? (char) cfg.fg : scr_fg(scr);
            const char bg = cfg.bg >= 0 ? (char) cfg.bg : scr_bg(scr);
            scr_set_scheme(scr, fg, bg);
            scr_clear(scr);
        }
    } else {
        cfg.tab_size = scr_tab_size(scr);
        cfg.fg = scr_fg(scr);
        cfg.bg = scr_bg(scr);
        cfg_save(&cfg, CFG_PATH);
    }
    ed->selecting_ = false;
    ed->anchor_.line = 1;
    ed->anchor_.x = 0;
    if (clip_init(&ed->clip_, CLIP_SIZE) == NULL) {
        return NULL;
    }

    if (!tb_init(&ed->buf_, mem_kb, fname)) {
       return NULL;
    }
    if (!ui_init(&ed->ui_, 256, ed->scr_.bottomY_, ed->scr_.cols_)) {
        tb_destroy(&ed->buf_);
        return NULL;
    }

    if (tb_used(&ed->buf_) > 0) {
        cmd_show(ed);
    }
    return ed;
}

void ed_destroy(editor* ed) {
    clip_destroy(&ed->clip_);
    ui_destroy(&ed->ui_);
    scr_destroy(&ed->scr_);
    tb_destroy(&ed->buf_);
}


// Shift with a motion key starts a selection if there is none and extends it if
// there is. Anything else ends it -- which is what makes the mode invisible:
// there is nothing to leave deliberately, and no way to get stuck in it.
// The commands that act on the selection rather than replacing or ending it.
// They manage it themselves: copy leaves it alone, cut and paste consume it,
// and select-all makes one.
static bool owns_selection(cmd_op cmd) {
    return cmd == cmd_copy || cmd == cmd_cut
        || cmd == cmd_paste || cmd == cmd_select_all;
}

sel_action ed_selection_for(editor* ed, key_command kc) {
    // Pressing shift is not a keystroke that ends anything. MOS reports it as
    // its own event before the arrow it modifies, so treating it as an ordinary
    // key would cancel the selection a moment before extending it.
    if (ed_is_modifier(kc.k.vkey)) {
        return SEL_NONE;
    }
    // Copy, cut, paste and select-all are about the selection, so they are not
    // keys that end it. What happens to it is each command's own business.
    if (owns_selection(kc.cmd)) {
        return SEL_NONE;
    }
    if ((kc.mods & MOD_SHFT) && ed_is_motion(kc.k.vkey)) {
        if (!ed->selecting_) {
            ed->anchor_ = tb_tell(&ed->buf_);
            ed->selecting_ = true;
        }

        return SEL_EXTEND;
    }
    if (ed->selecting_) {
        // A key that puts something in the document or takes something out
        // replaces the selection rather than acting next to it. selecting_ is
        // left set so the caller can still see what to delete; deleting it is
        // what clears it.
        if (ed_key_edits(kc)) {
            return SEL_REPLACE;
        }
        ed->selecting_ = false;

        return SEL_DROP;
    }

    return SEL_NONE;
}

void ed_selection_repaint(editor* ed, sel_action act, char y_before,
                          int top_before, int origin_before) {
    if (act == SEL_NONE) {
        return;
    }

    screen* scr = &ed->scr_;
    text_buffer* tb = &ed->buf_;
    const int top_after = tb_ypos(tb) - (scr->currY_ - scr->topY_);

    // The whole text area whenever the view moved under the text. Dropping a
    // selection has to clear a highlight that could be anywhere on screen; a
    // vertical scroll puts different lines on every row; and originX_ is
    // screen-wide, so a horizontal scroll shifts every row at once and leaves
    // the ones not repainted showing their old columns.
    if (act == SEL_DROP
        || top_after != top_before
        || scr->originX_ != origin_before) {
        cmd_repaint_rows(ed, scr->topY_, scr->bottomY_);
    } else {
        // Otherwise the highlight only changed between where the cursor was and
        // where it is, which for an arrow key is a row or two. A full repaint
        // costs 33-47ms on the VDP and this runs on every keystroke.
        const char lo = y_before < scr->currY_ ? y_before : scr->currY_;
        const char hi = y_before < scr->currY_ ? scr->currY_ : y_before;
        cmd_repaint_rows(ed, lo, hi);
    }
    scr_show_cursor_ch(scr, tb_peek(tb));
}

bool ed_key_edits(key_command kc) {
    // The switch is kept to itself and the command pointer tested after it.
    // Mixing the two in one expression makes the eZ80 backend fall over with
    // "unable to legalize instruction ... i15", which is a compiler bug rather
    // than anything wrong with the code -- but this shape avoids it and reads
    // no worse.
    switch (kc.k.vkey) {
        case VK_BACKSPACE:
        case VK_DELETE:
        case VK_KP_DELETE:
        case VK_RETURN:
        case VK_KP_ENTER:
        case VK_TAB:
            return true;
        default:
            break;
    }

    return kc.cmd == CMD_PUTC;
}

bool ed_is_modifier(VKey vkey) {
    switch (vkey) {
        case VK_LSHIFT: case VK_RSHIFT:
        case VK_LCTRL:  case VK_RCTRL:
        case VK_LALT:   case VK_RALT:
        case VK_LGUI:   case VK_RGUI:
            return true;
        default:
            return false;
    }
}

bool ed_is_motion(VKey vkey) {
    switch (vkey) {
        case VK_LEFT:     case VK_KP_LEFT:
        case VK_RIGHT:    case VK_KP_RIGHT:
        case VK_UP:       case VK_KP_UP:
        case VK_DOWN:     case VK_KP_DOWN:
        case VK_HOME:     case VK_KP_HOME:
        case VK_END:      case VK_KP_END:
        case VK_PAGEUP:   case VK_KP_PAGEUP:
        case VK_PAGEDOWN: case VK_KP_PAGEDOWN:
            return true;
        default:
            return false;
    }
}

void ed_run(editor* ed) {
    text_buffer* buf = &ed->buf_;
    screen* scr = &ed->scr_;

    for (;;) {
        scr_footer(scr, tb_fname(buf), tb_changed(buf), tb_xpos(buf), tb_ypos(buf));
        key_command kc = read_input();

        const sel_action act = ed_selection_for(ed, kc);

        // Where the view was, so the repaint afterwards can tell a cursor that
        // moved within the screen from one that moved the screen.
        const char y_before = scr->currY_;
        const int top_before = tb_ypos(buf) - (y_before - scr->topY_);
        const int origin_before = scr->originX_;

        if (act == SEL_REPLACE) {
            cmd_delete_selection(ed);
            // For BACKSPACE and DELETE that was the whole action: they mean
            // "remove this", and this was the selection.
            if (kc.k.vkey == VK_BACKSPACE || kc.k.vkey == VK_DELETE
                || kc.k.vkey == VK_KP_DELETE) {
                kc.cmd = NULL;
            }
        }

        if (kc.cmd == CMD_PUTC) {
            cmd_putc(ed, kc.k);
        } else if (kc.cmd == CMD_QUIT) {
            if (cmd_quit(ed)) {
                break;
            }
        } else if (kc.cmd == CMD_SAVE) {
            cmd_save(ed);
        } else if (kc.cmd != NULL) {
            kc.cmd(ed);
        }

        // A replace leaves no selection and has moved the text below it, so it
        // repaints like a drop: the whole area.
        ed_selection_repaint(ed, act == SEL_REPLACE ? SEL_DROP : act,
                             y_before, top_before, origin_before);
    }
    // Leaving the screen is scr_destroy's job: it restores the entry colours
    // first, so the clear lands in the user's background rather than AED's.
}

key_command ctrlCmds(key_command kc, char mods) {
    switch (kc.k.vkey) {
        case VK_q:
        case VK_Q:
            kc.cmd = CMD_QUIT;
            break;
        case VK_LEFT:
        case VK_KP_LEFT:
            kc.cmd = cmd_w_left;
            break;
        case VK_RIGHT:
        case VK_KP_RIGHT:
            kc.cmd = cmd_w_right;
            break;
        case VK_DELETE:
        case VK_KP_DELETE:
        case VK_d:
        case VK_D:
            kc.cmd = cmd_del_line;
            break;
        case VK_S:
        case VK_s:
		    if (mods & MOD_ALT) {
                kc.cmd = cmd_save_as;
            } else {
                kc.cmd = CMD_SAVE;
            }
            break;
        case VK_C:
        case VK_c:
            if (mods & MOD_ALT) {
                kc.cmd = cmd_color_picker;
            } else {
                kc.cmd = cmd_copy;
            }
            break;
        case VK_G:
        case VK_g:
            kc.cmd = cmd_goto;
            break;
        case VK_O:
        case VK_o:
            kc.cmd = cmd_open;
            break;
        case VK_A:
        case VK_a:
            kc.cmd = cmd_select_all;
            break;
        case VK_X:
        case VK_x:
            kc.cmd = cmd_cut;
            break;
        case VK_V:
        case VK_v:
            kc.cmd = cmd_paste;
            break;
        default:
            kc.cmd = NULL;
            break;
    }
    return kc;
}

key_command editCmds(key_command kc) {
    switch (kc.k.vkey) {
        case VK_LEFT:
        case VK_KP_LEFT:
            kc.cmd = cmd_left;
            break;
        case VK_RIGHT:
        case VK_KP_RIGHT:
            kc.cmd = cmd_right;
            break;
        case VK_BACKSPACE:
            kc.cmd = cmd_bksp;
            break;
        case VK_DELETE:
        case VK_KP_DELETE:
            kc.cmd = cmd_del;
            break;
        case VK_HOME:
        case VK_KP_HOME:
            kc.cmd = cmd_home;
            break;
        case VK_END:
        case VK_KP_END:
            kc.cmd = cmd_end;
            break;
        case VK_RETURN:
        case VK_KP_ENTER:
            kc.cmd = cmd_newl;
            break;
        case VK_UP:
        case VK_KP_UP:
            kc.cmd = cmd_up;
            break;
        case VK_DOWN:
        case VK_KP_DOWN:
            kc.cmd = cmd_down;
            break;
        case VK_PAGEUP:
            kc.cmd = cmd_page_up;
            break;
        case VK_PAGEDOWN:
            kc.cmd = cmd_page_down;
            break;
        default:
            break;
    }
    return kc;
}

key_command read_input(void) {
    key_command kc = {NULL, {'\0', VK_NONE}, 0};

    const key_event ev = input_read();
    kc.k.key = ev.ascii;
    kc.k.vkey = ev.vkey;
    kc.mods = ev.mods;

    const char mods = kc.mods;
    if (mods & MOD_CTRL) {
        return ctrlCmds(kc, mods);
    }

    if (kc.k.key == '\t' || (kc.k.key != 0x7F && kc.k.key >= 32)) {
        kc.cmd = CMD_PUTC;
    } else {
        return editCmds(kc);
    }

    return kc;
}

