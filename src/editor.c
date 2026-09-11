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
#include <string.h>

#include "cmd_ops.h"
#include "config.h"
#include "keys.h"

#define DEFAULT_CURSOR 32

// How far ed_init got before it gave up, so that exactly what was built can be
// taken back down again.
//
// The screen is the one that matters. scr_init runs before anything that can
// fail, and by then it has measured the colours the machine was using, put the
// user's scheme on, and possibly loaded a font. Returning without scr_destroy
// hands back a machine still wearing all of it -- which is what naming a file
// too large for memory used to do.
typedef enum _ed_built {
    ED_NOTHING = 0,
    ED_SCREEN,
    ED_CLIP,
    ED_BUF,
    ED_UNDO,
} ed_built;

static editor* ed_failed(editor* ed, ed_built built) {
    if (built >= ED_UNDO) {
        tb_set_undo(&ed->buf_, NULL);
        undo_destroy(&ed->undo_);
    }
    if (built >= ED_CLIP) {
        clip_destroy(&ed->clip_);
    }
    if (built >= ED_SCREEN) {
        scr_destroy(&ed->scr_);
    }
    if (built >= ED_BUF) {
        tb_destroy(&ed->buf_);
    }

    return NULL;
}

// Said after the screen has been handed back, so that it lands on the user's
// own screen and stays there. scr_destroy clears, so anything written before it
// is wiped by the very act of putting the colours right.
static void ed_say(const char* msg) {
    mos_puts((char*) msg, strlen(msg), 0);
    mos_puts("\r\n", 2, 0);
}

editor* ed_init(editor* ed, int mem_kb, const char* fname) {
    screen* scr = scr_init(&ed->scr_, DEFAULT_CURSOR);

    // Settings are read once at startup. The setters clamp or reject out of
    // range values, so a bad number in the file falls back rather than
    // rejecting the file -- there is nowhere useful to report an error to.
    //
    // On first run there is no file. Write one holding what AED is starting
    // with, including the colours it just measured off the Agon, so the user
    // has something to edit instead of a format to guess at.
    bool font_asked = false;
    bool font_loaded = false;

    config cfg;
    cfg_defaults(&cfg);
    if (cfg_load(&cfg, CFG_PATH)) {
        if (cfg.tab_size >= 0) {
            scr_set_tab_size(scr, (char) cfg.tab_size);
        }
        // Only when the file asks for it: see scr_set_ctrl_pause_frames.
        if (cfg.ctrl_pause >= 0) {
            scr_set_ctrl_pause_frames(scr, cfg.ctrl_pause);
        }
        // Before the colours and before anything is drawn: a font changes how
        // many rows there are, and everything below is sized in rows. Only when
        // the file asks for it, for the same reason as the line above -- see
        // scr_load_font. A font that will not load is not worth stopping for;
        // the editor runs in whatever font the machine already had.
        if (cfg.font[0] != 0) {
            font_asked = true;
            font_loaded = scr_load_font(scr, cfg.font);
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
        return ed_failed(ed, ED_SCREEN);
    }

    // Made empty and loaded separately, so that a file that will not load can
    // be reported by name and by reason -- tb_init only says whether it worked.
    if (tb_init(&ed->buf_, mem_kb, NULL) == NULL) {
        return ed_failed(ed, ED_CLIP);
    }
    if (fname != NULL) {
        const tb_result why = tb_load(&ed->buf_, fname);
        if (why != TB_OK) {
            ed_failed(ed, ED_BUF);
            ed_say(why == TB_TOO_LARGE ? "file too large" : "invalid file");

            return NULL;
        }
    }

    // Attached after the load, on purpose. tb_load normalises the file's line
    // endings, and those writes go through the same primitives an edit does --
    // with a log already in place the first undo would unpick the file's own
    // CRLFs.
    ed->find_[0] = 0;
    ed->findsz_ = 0;

    if (undo_init(&ed->undo_, UNDO_TEXT_BYTES, UNDO_MAX_RECS) == NULL) {
        return ed_failed(ed, ED_BUF);
    }
    tb_set_undo(&ed->buf_, &ed->undo_);
    if (!ui_init(&ed->ui_, 256, ed->scr_.bottomY_, ed->scr_.cols_)) {
        return ed_failed(ed, ED_UNDO);
    }

    // The document is only drawn when there is something in it -- painting a
    // screenful of spaces over an already-cleared screen is 1800 bytes down the
    // VDP link for nothing. The cursor is drawn either way, which it was not:
    // starting AED with no file left no cursor on screen at all until the first
    // keystroke happened to repaint the row it was on.
    // A font was asked for and did not load: the file is missing, or it is not
    // a whole number of 256-byte rows, or this VDP has no font API. Whichever
    // it was, saying nothing leaves the stock font on screen and no reason for
    // it -- and the setting sits in a file edited by hand, so a typo in the
    // path is the likeliest cause and the least guessable.
    //
    // It waits for a key. That is an interruption at startup, which is the
    // point: it is a mistake in a settings file, and it will happen every time
    // until it is fixed.
    if (font_asked && !font_loaded) {
        // Built by hand rather than with snprintf. This is the program's only
        // formatted print, and asking for it links nanoprintf: 4,994 bytes,
        // eight per cent of the binary, for one %s.
        static const char lead[] = "font not loaded: ";
        static char msg[CFG_FONT_MAX + sizeof(lead)];
        const int lead_n = (int) sizeof(lead) - 1;
        int n = (int) strlen(cfg.font);
        if (n > (int) sizeof(msg) - lead_n - 1) {
            n = (int) sizeof(msg) - lead_n - 1;
        }
        memcpy(msg, lead, (size_t) lead_n);
        memcpy(msg + lead_n, cfg.font, (size_t) n);
        msg[lead_n + n] = 0;
        ui_message(&ed->ui_, &ed->scr_, msg);
        scr_clear(&ed->scr_);
    }

    ed->banner_ = false;
    if (tb_used(&ed->buf_) > 0) {
        cmd_show(ed);
    } else {
        // Nothing to show, so say what this is and where the commands are.
        // Only when no file was named: opening an empty file is a different
        // thing from starting with nothing, and someone who named a file has
        // already said what they came to do.
        if (fname == NULL) {
            ui_banner(&ed->ui_, &ed->scr_);
            ed->banner_ = true;
        }
        scr_show_cursor_ch(&ed->scr_, tb_peek(&ed->buf_));
    }

    // Last, so that no failure above has to take it back down again.
    keys_open();

    return ed;
}

void ed_destroy(editor* ed) {
    keys_close();
    tb_set_undo(&ed->buf_, NULL);
    undo_destroy(&ed->undo_);
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
                          char x_before, int top_before, int origin_before) {
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
    } else if (y_before == scr->currY_) {
        // The cursor stayed on its row, so the highlight changed only between
        // the column it was in and the one it is in now -- a character for an
        // arrow, a word for CTRL with one. The rest of the row already shows
        // what it should, and a row is eighty characters down a serial link.
        // One column past the far end, because the cell the cursor vacated has
        // to be repainted as ordinary text.
        const char from = x_before < scr->currX_ ? x_before : scr->currX_;
        const char to = x_before < scr->currX_ ? scr->currX_ : x_before;
        cmd_repaint_span(ed, scr->currY_, scr->originX_ + from,
                         scr->originX_ + to + 1);
    } else {
        // It changed rows without the view moving, so both rows need doing.
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

bool ed_footer_wanted(char held) {
    return (held & (MOD_CTRL | MOD_SHFT)) != (MOD_CTRL | MOD_SHFT);
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
        // Not while a chord is held down. The footer sits on the bottom row,
        // so drawing it means moving the cursor off the text, writing, and
        // moving back -- and doing that between keystrokes is what stops the
        // next one arriving. Bisected to this call: the same editor with the
        // footer drawn fails, and without it works.
        //
        // These are the modifiers held *now* rather than the ones that came
        // with the last key, which is the more honest question to ask -- but
        // it does not make the footer return any sooner. The loop only gets
        // here when a key arrives, and a modifier being released is not one:
        // keys_wait drops those. So the footer comes back on the next key
        // pressed after the chord, not on the release itself.
        //
        // Reading them is a load through the sysvars pointer, not a call into
        // MOS, so asking costs nothing on the path this is protecting.
        if (ed_footer_wanted((char) getsysvar_keymods())) {
            scr_footer(scr, tb_fname(buf), tb_changed(buf),
                       tb_xpos(buf), tb_ypos(buf));
        }
        key_command kc = read_input();

        ed_clear_banner(ed);

        const sel_action act = ed_selection_for(ed, kc);

        // Where the view was, so the repaint afterwards can tell a cursor that
        // moved within the screen from one that moved the screen.
        const char y_before = scr->currY_;
        const char x_before = scr->currX_;
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
                             y_before, x_before, top_before, origin_before);
    }
    // Leaving the screen is scr_destroy's job: it restores the entry colours
    // first, so the clear lands in the user's background rather than AED's.
}

// The banner goes on the first key, whatever it was.
//
// The whole text area is cleared rather than painted over: a keystroke repaints
// the row it is on and nothing else, so the rest of the banner would sit behind
// the document until something else happened to cover it. Does nothing when no
// banner is up, which is every pass of the loop but the first.
void ed_clear_banner(editor* ed) {
    if (!ed->banner_) {
        return;
    }
    ed->banner_ = false;
    scr_clear_textarea(&ed->scr_, ed->scr_.topY_, (char) (ed->scr_.bottomY_ - 1));
    scr_show_cursor_ch(&ed->scr_, tb_peek(&ed->buf_));
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
            kc.cmd = cmd_copy;
            break;
        case VK_G:
        case VK_g:
            kc.cmd = cmd_goto;
            break;
        case VK_H:
        case VK_h:
            kc.cmd = cmd_help;
            break;
        case VK_E:
        case VK_e:
            kc.cmd = cmd_settings;
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
        case VK_F:
        case VK_f:
            kc.cmd = cmd_find;
            break;
        case VK_N:
        case VK_n:
            kc.cmd = cmd_find_next;
            break;
        case VK_P:
        case VK_p:
            kc.cmd = cmd_find_prev;
            break;
        case VK_Z:
        case VK_z:
            kc.cmd = cmd_undo;
            break;
        case VK_Y:
        case VK_y:
            kc.cmd = cmd_redo;
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

    // One event carries the key and the modifiers held with it, so a chord
    // arrives whole. Reading them separately -- a character, then the keymods
    // sysvar -- is what used to lose CTRL+SHIFT+<arrow>: the chord produces no
    // character to read.
    const key_press kp = keys_wait();
    kc.k.key = kp.ch;
    kc.k.vkey = kp.vkey;
    kc.mods = kp.mods;

    if (kp.mods & MOD_CTRL) {
        return ctrlCmds(kc, kp.mods);
    }

    if (kc.k.key == '\t' || (kc.k.key != 0x7F && kc.k.key >= 32)) {
        kc.cmd = CMD_PUTC;
    } else {
        return editCmds(kc);
    }

    return kc;
}

