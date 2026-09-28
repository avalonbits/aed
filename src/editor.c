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

#include "app.h"
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
        tb_set_undo(&ed->doc_.buf_, NULL);
        undo_destroy(&ed->doc_.undo_);
    }
    if (built >= ED_CLIP) {
        clip_destroy(&ed->clip_);
    }
    if (built >= ED_SCREEN) {
        scr_destroy(&ed->scr_);
    }
    if (built >= ED_BUF) {
        tb_destroy(&ed->doc_.buf_);
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

/*
 * Where AED keeps its files, handed to the core by ed_init.
 *
 * The settings file is an INI file and is named like one now. The old name is
 * still read once: cfg_migrate copies it across and takes it away, so a card
 * that has been through an older AED comes up with the same settings under the
 * new name and nothing is left behind to wonder about.
 *
 * An .ini beside a .cfg means the move has already happened and something put
 * the .cfg back -- an older AED run from the same card, or a backup copied by
 * hand. The .ini wins and the .cfg is left alone rather than read or removed;
 * it is not this program's to delete once it has stopped being its file.
 *
 * Grammars, themes and fonts live beside the settings file, in /config/aed,
 * each kind of file in a directory of its own, so a user can add one by
 * dropping a file on the card.
 */
const app_context AED_APP = {
    .name       = "aed",
    .cfg_path   = CFG_DIR "/aed.ini",
    .cfg_old    = CFG_DIR "/aed.cfg",
    .syntax_dir = CFG_DIR "/aed/syntax",
    .theme_dir  = CFG_DIR "/aed/themes",
    .font_dir   = CFG_DIR "/aed",
};

// The document's syntax window is sized for the tallest view the core allows;
// AED's screen must fit inside it.
_Static_assert(SCR_MAX_ROWS <= DOC_ROWS_MAX,
               "a screen taller than a document's syntax window");

#define SYN_PATH_MAX 64

static int join_path(char* out, int max, const char* dir, const char* name) {
    const int d = (int) strlen(dir);
    const int n = (int) strlen(name);
    if (d + 1 + n + 1 > max) {
        return 0;
    }
    memcpy(out, dir, (size_t) d);
    out[d] = '/';
    memcpy(out + d + 1, name, (size_t) n);
    out[d + 1 + n] = 0;

    return d + 1 + n;
}

/*
 * The first grammar in the directory that claims this file name.
 *
 * Every candidate is read in full before it can be asked, because the
 * extensions it claims are in the file. Three grammars ship and each is a
 * couple of kilobytes, so the walk costs a few reads on open; a directory of
 * dozens would want the header read on its own, which is an optimisation and
 * not a change of shape.
 *
 * A grammar that loads and does not claim the file is left behind by the next
 * one, and the last is cleared on the way out -- so a miss leaves an empty
 * grammar rather than whichever file happened to sort last.
 */
static bool grammar_for(syntax* g, const char* fname) {
    // Static, as font_list is and for the same reason: a FILINFO carries a
    // 256-byte name, and on the stack those bytes take the frame past the 128
    // an ix displacement reaches.
    static DIR dir;
    static FILINFO info;
    static char path[SYN_PATH_MAX];

    const char* from = app_get()->syntax_dir;
    if (ffs_dopen(&dir, from) != 0) {
        return false;
    }
    bool got = false;
    while (!got) {
        if (ffs_dread(&dir, &info) != 0 || info.fname[0] == 0) {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0) {
            continue;
        }
        if (join_path(path, SYN_PATH_MAX, from, info.fname) == 0) {
            continue;
        }
        if (!syn_load(g, path)) {
            continue;
        }
        got = syn_covers(g, fname);
    }
    ffs_dclose(&dir);
    if (!got) {
        syn_clear(g);
    }

    return got;
}

// The first theme whose author meant it for this background.
/*
 * The theme for a background: the first whose `covers` lists it, and failing
 * that the one whose covered backgrounds are nearest it in brightness.
 *
 * The first rule is the author's word and always wins. The second is for the
 * backgrounds nobody listed, which in a 64 colour mode is most of them: a
 * theme's covered backgrounds say what brightness its colours were chosen
 * against, so the nearest is the best guess at one that reads. Too far from
 * every theme -- THEME_NEAR -- and there is no theme, and the file keeps the
 * reader's own colours rather than be painted in something unreadable.
 */
static bool theme_for(theme* t, int bg) {
    static DIR dir;
    static FILINFO info;
    static char path[SYN_PATH_MAX];
    static char nearest[SYN_PATH_MAX];

    const char* from = app_get()->theme_dir;
    if (ffs_dopen(&dir, from) != 0) {
        return false;
    }
    bool got = false;
    int best = -1;
    nearest[0] = 0;
    while (!got) {
        if (ffs_dread(&dir, &info) != 0 || info.fname[0] == 0) {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0) {
            continue;
        }
        if (join_path(path, SYN_PATH_MAX, from, info.fname) == 0) {
            continue;
        }
        if (!theme_load(t, path)) {
            continue;
        }
        got = theme_covers(t, bg);
        if (!got) {
            const int d = theme_distance(t, bg);
            if (d >= 0 && d <= THEME_NEAR && (best < 0 || d < best)) {
                best = d;
                strcpy(nearest, path);
            }
        }
    }
    ffs_dclose(&dir);
    // The nearest was loaded and then overwritten by the themes read after it,
    // so it is read again. Once, and only when nothing covered the background.
    if (!got && best >= 0) {
        got = theme_load(t, nearest);
    }
    if (!got) {
        theme_clear(t);
    }

    return got;
}

// The fewest colours a mode needs before a document is highlighted in it.
#define SYN_MIN_COLOURS 16

void ed_pick_syntax(editor* ed) {
    if (ed == NULL) {
        return;
    }
    // What was worked out belongs to the document that was there before this.
    ed->doc_.synFirst_ = 0;
    ed->doc_.synKnown_ = 0;
    ed->scr_.v_->synTop_ = 1;
    syn_clear(&ed->doc_.syn_);
    theme_clear(&ed->theme_);
    scr_set_theme(&ed->scr_, NULL);
    scr_base_restore(&ed->scr_);

    /*
     * Only where the screen has the colours to say it with.
     *
     * A theme names colours out of the sixteen every Agon mode of that depth
     * has, and a mode with fewer shows the index modulo what it has. In a two
     * colour mode that sends most of a C file to colour 0 -- black on a black
     * background, so the document vanishes as it is coloured. Four colours is
     * no better in kind: a theme's choices collide with the background
     * unpredictably. So below sixteen there is no highlighting, and the
     * document is drawn in the reader's own pair as a file with no grammar is.
     */
    if (ed->scr_.colors_ < SYN_MIN_COLOURS) {
        return;
    }
    const char* fname = tb_fname(&ed->doc_.buf_);
    if (fname == NULL || fname[0] == 0) {
        return;                 // a document with no name has no extension
    }
    if (!grammar_for(&ed->doc_.syn_, fname)) {
        return;
    }
    if (!theme_for(&ed->theme_, scr_base_bg(&ed->scr_))) {
        // A grammar with no theme to colour it by would divide the line into
        // tokens and paint every one of them the same, which is the work
        // without the result.
        syn_clear(&ed->doc_.syn_);

        return;
    }
    scr_set_theme(&ed->scr_, &ed->theme_);

    /*
     * A theme may move the pair the document is drawn on, and moves only the
     * active one. What the user chose is untouched and is what aed.ini keeps,
     * so opening a file with no grammar -- which calls scr_base_restore above
     * -- puts their colours back.
     *
     * A theme that says nothing about fg and bg leaves both alone, which is
     * what the three shipped themes do.
     */
    scr_theme_scheme(&ed->scr_, ed->theme_.fg, ed->theme_.bg);
}

editor* ed_init(editor* ed, int mem_kb, const char* fname) {
    // First, because everything below may ask the core where AED's files are.
    app_set(&AED_APP);
    const app_context* app = app_get();

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
    // Before anything reads them: a card written by an older AED has the
    // settings under the old name, and this is the one run that moves them.
    const bool moved = cfg_migrate(app->cfg_old, app->cfg_path);

    if (cfg_load(&cfg, app->cfg_path)) {
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
    } else if (moved) {
        cfg.tab_size = scr_tab_size(scr);
        // The user's pair, so a theme in force when the settings are written
        // does not become the user's setting.
        cfg.fg = scr_base_fg(scr);
        cfg.bg = scr_base_bg(scr);
        cfg_save(&cfg, app->cfg_path);
    }
    /*
     * And when the move could not finish, nothing is written at all. The old
     * file still holds the reader's settings and the next run will try again;
     * a fresh one written now would be found first from then on, and their
     * settings would sit in a file nothing reads.
     */
    ed->doc_.selecting_ = false;
    ed->doc_.anchor_.line = 1;
    ed->doc_.anchor_.x = 0;
    if (clip_init(&ed->clip_, CLIP_SIZE) == NULL) {
        return ed_failed(ed, ED_SCREEN);
    }

    // Made empty and loaded separately, so that a file that will not load can
    // be reported by name and by reason -- tb_init only says whether it worked.
    if (tb_init(&ed->doc_.buf_, mem_kb, NULL) == NULL) {
        return ed_failed(ed, ED_CLIP);
    }
    if (fname != NULL) {
        const tb_result why = tb_load(&ed->doc_.buf_, fname);
        if (why != TB_OK) {
            ed_failed(ed, ED_BUF);
            ed_say(why == TB_TOO_LARGE ? "file too large" : "invalid file");

            return NULL;
        }
    }

    // Somewhere for the screen to ask about colour. Once, before anything is
    // painted: every paint from here on is coloured without knowing it.
    ed_attach_colourer(ed);

    // After the load, because the grammar is chosen by the document's name and
    // the buffer does not have one until it is loaded.
    ed_pick_syntax(ed);

    // Attached after the load, on purpose. tb_load normalises the file's line
    // endings, and those writes go through the same primitives an edit does --
    // with a log already in place the first undo would unpick the file's own
    // CRLFs.
    ed->find_[0] = 0;
    ed->findsz_ = 0;

    if (undo_init(&ed->doc_.undo_, UNDO_TEXT_BYTES, UNDO_MAX_RECS) == NULL) {
        return ed_failed(ed, ED_BUF);
    }
    tb_set_undo(&ed->doc_.buf_, &ed->doc_.undo_);
    if (!ui_init(&ed->ui_, 256, ed->scr_.v_->bottomY_, ed->scr_.v_->cols_)) {
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
    ed->keys_ = &AED_KEYS;
    ed->leaving_ = false;
    if (tb_used(&ed->doc_.buf_) > 0) {
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
        scr_show_cursor_ch(&ed->scr_, tb_peek(&ed->doc_.buf_));
    }


    // Last, so that no failure above has to take it back down again.
    keys_open();

    return ed;
}

void ed_destroy(editor* ed) {
    keys_close();
    tb_set_undo(&ed->doc_.buf_, NULL);
    undo_destroy(&ed->doc_.undo_);
    clip_destroy(&ed->clip_);
    ui_destroy(&ed->ui_);
    scr_destroy(&ed->scr_);
    tb_destroy(&ed->doc_.buf_);
}

// Shift with a motion key starts a selection if there is none and extends it if
// there is. Anything else ends it -- which is what makes the mode invisible:
// there is nothing to leave deliberately, and no way to get stuck in it.
// The commands that act on the selection rather than replacing or ending it.
// They manage it themselves: copy leaves it alone, cut and paste consume it,
// and select-all makes one.
sel_action ed_selection_for(editor* ed, key_command kc) {
    // Pressing shift is not a keystroke that ends anything. MOS reports it as
    // its own event before the arrow it modifies, so treating it as an ordinary
    // key would cancel the selection a moment before extending it.
    if (ed_is_modifier(kc.k.vkey)) {
        return SEL_NONE;
    }
    // Copy, cut, paste and select-all are about the selection, so they are not
    // keys that end it. What happens to it is each command's own business.
    //
    // Find is about the selection too: a match is left selected, and the next
    // search measures from where that selection starts. Dropping it here left
    // cmd_find_prev searching back from the cursor -- which is at the *end* of
    // the match it is standing on, so it found the same one again and CTRL+P
    // appeared to do nothing. The test of it called the command directly and
    // so passed; it now goes through the keymap.
    if (kc.flags & KC_OWNS_SEL) {
        return SEL_NONE;
    }
    if ((kc.mods & MOD_SHFT) && ed_is_motion(kc.k.vkey)) {
        if (!ed->doc_.selecting_) {
            ed->doc_.anchor_ = tb_tell(&ed->doc_.buf_);
            ed->doc_.selecting_ = true;
        }

        return SEL_EXTEND;
    }
    if (ed->doc_.selecting_) {
        // A key that puts something in the document or takes something out
        // replaces the selection rather than acting next to it. selecting_ is
        // left set so the caller can still see what to delete; deleting it is
        // what clears it.
        if (kc.flags & KC_EDITS) {
            return SEL_REPLACE;
        }
        ed->doc_.selecting_ = false;

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
    text_buffer* tb = &ed->doc_.buf_;
    const int top_after = tb_ypos(tb) - (scr->v_->currY_ - scr->v_->topY_);

    // The whole text area whenever the view moved under the text. Dropping a
    // selection has to clear a highlight that could be anywhere on screen; a
    // vertical scroll puts different lines on every row; and originX_ is
    // screen-wide, so a horizontal scroll shifts every row at once and leaves
    // the ones not repainted showing their old columns.
    if (act == SEL_DROP
        || top_after != top_before
        || scr->v_->originX_ != origin_before) {
        cmd_repaint_rows(ed, scr->v_->topY_, scr->v_->bottomY_);
    } else if (y_before == scr->v_->currY_) {
        // The cursor stayed on its row, so the highlight changed only between
        // the column it was in and the one it is in now -- a character for an
        // arrow, a word for CTRL with one. The rest of the row already shows
        // what it should, and a row is eighty characters down a serial link.
        // One column past the far end, because the cell the cursor vacated has
        // to be repainted as ordinary text.
        const char from = x_before < scr->v_->currX_ ? x_before : scr->v_->currX_;
        const char to = x_before < scr->v_->currX_ ? scr->v_->currX_ : x_before;
        cmd_repaint_span(ed, scr->v_->currY_, scr->v_->originX_ + from,
                         scr->v_->originX_ + to + 1);
    } else {
        // It changed rows without the view moving, so both rows need doing.
        const char lo = y_before < scr->v_->currY_ ? y_before : scr->v_->currY_;
        const char hi = y_before < scr->v_->currY_ ? scr->v_->currY_ : y_before;
        cmd_repaint_rows(ed, lo, hi);
    }
    scr_show_cursor_ch(scr, tb_peek(tb));
}

// The keys that put something in the document or take something out, whatever
// they are bound to. With a selection live these replace it rather than acting
// alongside it, so they have to be told apart from the ones that merely end it.
// A property of the key rather than of a binding: CTRL+BACKSPACE is bound to
// nothing and still takes a selection away.
static bool key_edits(VKey vkey) {
    switch (vkey) {
        case VK_BACKSPACE:
        case VK_DELETE:
        case VK_KP_DELETE:
        case VK_RETURN:
        case VK_KP_ENTER:
        case VK_TAB:
            return true;
        default:
            return false;
    }
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

bool ed_handle(editor* ed, key_command kc) {
    text_buffer* buf = &ed->doc_.buf_;
    screen* scr = &ed->scr_;

    ed_clear_banner(ed);

    const sel_action act = ed_selection_for(ed, kc);

    // Where the view was, so the repaint afterwards can tell a cursor that
    // moved within the screen from one that moved the screen.
    const char y_before = scr->v_->currY_;
    const char x_before = scr->v_->currX_;
    const int top_before = tb_ypos(buf) - (y_before - scr->v_->topY_);
    const int origin_before = scr->v_->originX_;

    if (act == SEL_REPLACE) {
        cmd_delete_selection(ed);
        // For BACKSPACE and DELETE that was the whole action: they mean
        // "remove this", and this was the selection.
        if (kc.k.vkey == VK_BACKSPACE || kc.k.vkey == VK_DELETE
            || kc.k.vkey == VK_KP_DELETE) {
            kc.cmd = NULL;
            kc.flags &= (char) ~KC_PUTC;
        }
    }

    if (kc.flags & KC_PUTC) {
        cmd_putc(ed, kc.k);
    } else if (kc.cmd != NULL) {
        kc.cmd(ed);
    }
    // Leaving is decided before anything is settled or repainted: the screen
    // is about to be handed back.
    if (ed->leaving_) {
        return false;
    }

    // Once the command is done and before anything is repainted. A repaint
    // reads about a screenful either side of the cursor through walkers, and a
    // walker may not slide -- so whatever it is going to want has to be in
    // memory by now. On an unpaged document this is two comparisons and a
    // return.
    tb_settle(buf);

    // A replace leaves no selection and has moved the text below it, so it
    // repaints like a drop: the whole area.
    ed_selection_repaint(ed, act == SEL_REPLACE ? SEL_DROP : act,
                         y_before, x_before, top_before, origin_before);

    return true;
}

void ed_run(editor* ed) {
    text_buffer* buf = &ed->doc_.buf_;

    do {
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
            scr_footer(&ed->scr_, tb_fname(buf), tb_changed(buf),
                       tb_xpos(buf), tb_ypos(buf));
        }
    } while (ed_handle(ed, ed_translate(ed->keys_, keys_wait())));
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
    scr_clear_textarea(&ed->scr_, ed->scr_.v_->topY_, (char) (ed->scr_.v_->bottomY_ - 1));
    scr_show_cursor_ch(&ed->scr_, tb_peek(&ed->doc_.buf_));
}

void ed_cmd_save(editor* ed) {
    (void) cmd_save(ed);
}

void ed_cmd_quit(editor* ed) {
    if (cmd_quit(ed)) {
        ed->leaving_ = true;
    }
}

#define C MOD_CTRL
#define OWN KC_OWNS_SEL

// Both cases of a letter: MOS reports the shifted one when SHIFT is held.
#define LETTER(lo, up, mods, flags, cmd) \
    { lo, mods, flags, cmd }, { up, mods, flags, cmd }

static const key_binding AED_BINDINGS[] = {
    // With CTRL.
    LETTER(VK_q, VK_Q, C, 0, ed_cmd_quit),
    { VK_LEFT,      C, 0, cmd_w_left },
    { VK_KP_LEFT,   C, 0, cmd_w_left },
    { VK_RIGHT,     C, 0, cmd_w_right },
    { VK_KP_RIGHT,  C, 0, cmd_w_right },
    { VK_DELETE,    C, 0, cmd_del_line },
    { VK_KP_DELETE, C, 0, cmd_del_line },
    LETTER(VK_d, VK_D, C, 0, cmd_del_line),
    { VK_HOME,      C, 0, cmd_doc_top },
    { VK_KP_HOME,   C, 0, cmd_doc_top },
    { VK_END,       C, 0, cmd_doc_end },
    { VK_KP_END,    C, 0, cmd_doc_end },
    LETTER(VK_s, VK_S, C | MOD_ALT, 0, cmd_save_as),
    LETTER(VK_s, VK_S, C, 0, ed_cmd_save),
    LETTER(VK_c, VK_C, C, OWN, cmd_copy),
    LETTER(VK_g, VK_G, C, 0, cmd_goto),
    LETTER(VK_h, VK_H, C, 0, cmd_help),
    LETTER(VK_e, VK_E, C, 0, cmd_settings),
    LETTER(VK_o, VK_O, C, 0, cmd_open),
    LETTER(VK_a, VK_A, C, OWN, cmd_select_all),
    LETTER(VK_x, VK_X, C, OWN, cmd_cut),
    LETTER(VK_f, VK_F, C, OWN, cmd_find),
    LETTER(VK_n, VK_N, C, OWN, cmd_find_next),
    LETTER(VK_p, VK_P, C, OWN, cmd_find_prev),
    LETTER(VK_z, VK_Z, C, 0, cmd_undo),
    LETTER(VK_y, VK_Y, C, 0, cmd_redo),
    LETTER(VK_v, VK_V, C, OWN, cmd_paste),

    // Without it.
    { VK_LEFT,      0, 0, cmd_left },
    { VK_KP_LEFT,   0, 0, cmd_left },
    { VK_RIGHT,     0, 0, cmd_right },
    { VK_KP_RIGHT,  0, 0, cmd_right },
    { VK_BACKSPACE, 0, 0, cmd_bksp },
    { VK_DELETE,    0, 0, cmd_del },
    { VK_KP_DELETE, 0, 0, cmd_del },
    { VK_HOME,      0, 0, cmd_home },
    { VK_KP_HOME,   0, 0, cmd_home },
    { VK_END,       0, 0, cmd_end },
    { VK_KP_END,    0, 0, cmd_end },
    { VK_RETURN,    0, 0, cmd_newl },
    { VK_KP_ENTER,  0, 0, cmd_newl },
    { VK_UP,        0, 0, cmd_up },
    { VK_KP_UP,     0, 0, cmd_up },
    { VK_DOWN,      0, 0, cmd_down },
    { VK_KP_DOWN,   0, 0, cmd_down },
    { VK_PAGEUP,    0, 0, cmd_page_up },
    { VK_PAGEDOWN,  0, 0, cmd_page_down },
};

#undef LETTER
#undef OWN
#undef C

const keymap AED_KEYS = {
    AED_BINDINGS, (int) (sizeof(AED_BINDINGS) / sizeof(AED_BINDINGS[0])),
};

key_command ed_translate(const keymap* km, key_press kp) {
    key_command kc = {NULL, {'\0', VK_NONE}, 0, 0};

    // One event carries the key and the modifiers held with it, so a chord
    // arrives whole. Reading them separately -- a character, then the keymods
    // sysvar -- is what used to lose CTRL+SHIFT+<arrow>: the chord produces no
    // character to read.
    kc.k.key = kp.ch;
    kc.k.vkey = kp.vkey;
    kc.mods = kp.mods;
    if (key_edits(kp.vkey)) {
        kc.flags |= KC_EDITS;
    }

    const char ctrl = kp.mods & MOD_CTRL;
    if (!ctrl && (kp.ch == '\t' || (kp.ch != 0x7F && kp.ch >= 32))) {
        kc.flags |= KC_PUTC | KC_EDITS;

        return kc;
    }
    for (int i = 0; i < km->n; i++) {
        const key_binding* b = &km->keys[i];
        if (b->vkey != kp.vkey || (b->mods & MOD_CTRL) != ctrl) {
            continue;
        }
        if ((b->mods & kp.mods) != b->mods) {
            continue;
        }
        kc.cmd = b->cmd;
        kc.flags |= b->flags;
        break;
    }

    return kc;
}
