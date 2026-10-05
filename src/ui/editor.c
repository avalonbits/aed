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
        tb_set_undo(&ed->home_.buf_, NULL);
        undo_destroy(&ed->home_.undo_);
    }
    if (built >= ED_CLIP) {
        clip_destroy(&ed->clip_);
    }
    if (built >= ED_SCREEN) {
        scr_destroy(&ed->scr_);
    }
    if (built >= ED_BUF) {
        tb_destroy(&ed->home_.buf_);
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

/*
 * The theme for the document now current, by its grammar and the background in
 * force: cleared with the user's own colours put back when there is no grammar
 * or no theme for it, and set on the screen when there is. A grammar with no
 * theme to colour it by would divide the line into tokens and paint every one
 * of them the same, which is the work without the result, so it goes too.
 */
static void pick_theme(editor* ed) {
    theme_clear(&ed->theme_);
    scr_set_theme(&ed->scr_, NULL);
    scr_base_restore(&ed->scr_);
    if (!ed->doc_->syn_.loaded) {
        return;
    }
    if (!theme_for(&ed->theme_, scr_base_bg(&ed->scr_))) {
        syn_clear(&ed->doc_->syn_);

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

void ed_pick_syntax(editor* ed) {
    if (ed == NULL) {
        return;
    }
    // What was worked out belongs to the document that was there before this.
    ed->doc_->synFirst_ = 0;
    ed->doc_->synKnown_ = 0;
    ed->scr_.v_->synTop_ = 1;
    syn_clear(&ed->doc_->syn_);

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
    const char* fname = tb_fname(&ed->doc_->buf_);
    if (ed->scr_.colors_ >= SYN_MIN_COLOURS
            && fname != NULL && fname[0] != 0) {   // no name, no extension
        grammar_for(&ed->doc_->syn_, fname);
    }
    pick_theme(ed);
}

tb_result ed_doc_open(editor* ed, document* doc, view* v, int mem_kb,
                      const char* fname) {
    doc->selecting_ = false;
    doc->anchor_.line = 1;
    doc->anchor_.x = 0;
    doc->synFirst_ = 0;
    doc->synKnown_ = 0;
    syn_clear(&doc->syn_);

    // No room for another document is reported as one that will not fit,
    // which from where the reader stands is what it is.
    if (tb_init(&doc->buf_, mem_kb, NULL) == NULL) {
        return TB_TOO_LARGE;
    }
    if (fname != NULL) {
        const tb_result why = tb_load(&doc->buf_, fname);
        if (why != TB_OK) {
            tb_destroy(&doc->buf_);

            return why;
        }
    }
    // After the load, for the reason ed_init_for gives: tb_load normalises
    // line endings through the same primitives an edit uses.
    if (undo_init(&doc->undo_, UNDO_TEXT_BYTES, UNDO_MAX_RECS) == NULL) {
        tb_destroy(&doc->buf_);

        return TB_TOO_LARGE;
    }
    tb_set_undo(&doc->buf_, &doc->undo_);

    // Made current, with a fresh view of the whole text area, and shown from
    // the top -- the same reset opening a file into the current document gets.
    ed->doc_ = doc;
    scr_view_init(&ed->scr_, v);
    scr_set_view(&ed->scr_, v);
    ed_pick_syntax(ed);
    scr_clear(&ed->scr_);
    cmd_show(ed);

    return TB_OK;
}

void ed_doc_show(editor* ed, document* doc, view* v) {
    ed->doc_ = doc;
    scr_set_view(&ed->scr_, v);
    // Its grammar is its own and still loaded; the theme is the screen's, and
    // the background may have changed since this document was last shown.
    pick_theme(ed);
    cmd_restore_after_modal(ed, false);
}

void ed_doc_close(document* doc) {
    tb_set_undo(&doc->buf_, NULL);
    undo_destroy(&doc->undo_);
    tb_destroy(&doc->buf_);
    syn_clear(&doc->syn_);
}

editor* ed_init_for(editor* ed, int mem_kb, const char* fname,
                    const ed_program* prog) {
    // First, because everything below may ask the core where the program's
    // files are.
    app_set(prog->app);
    ed->doc_ = &ed->home_;

    scr_init(&ed->scr_, DEFAULT_CURSOR);
    // scr_init has drawn the header already, with no title in it -- there was
    // none to give it -- so it is drawn again with one. Settings would do that
    // as a side effect of clearing the screen, but only when the program has a
    // settings step and the file sets a colour; a program without one started
    // with an empty title bar until something else cleared the screen.
    scr_set_title(&ed->scr_, prog->title);
    scr_header_draw(&ed->scr_);

    // Before anything is sized: settings can change the font, and with it how
    // many rows there are.
    const char* say = prog->settings != NULL ? prog->settings(ed) : NULL;

    ed->doc_->selecting_ = false;
    ed->doc_->anchor_.line = 1;
    ed->doc_->anchor_.x = 0;
    if (clip_init(&ed->clip_, CLIP_SIZE) == NULL) {
        return ed_failed(ed, ED_SCREEN);
    }

    // Made empty and loaded separately, so that a file that will not load can
    // be reported by name and by reason -- tb_init only says whether it worked.
    if (tb_init(&ed->doc_->buf_, mem_kb, NULL) == NULL) {
        return ed_failed(ed, ED_CLIP);
    }
    if (fname != NULL) {
        const tb_result why = tb_load(&ed->doc_->buf_, fname);
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

    if (undo_init(&ed->doc_->undo_, UNDO_TEXT_BYTES, UNDO_MAX_RECS) == NULL) {
        return ed_failed(ed, ED_BUF);
    }
    tb_set_undo(&ed->doc_->buf_, &ed->doc_->undo_);
    if (!ui_init(&ed->ui_, 256, ed->scr_.v_->bottomY_, ed->scr_.v_->cols_)) {
        return ed_failed(ed, ED_UNDO);
    }

    // Whatever the settings step had to say, now that there is a prompt row to
    // say it on and before the document covers the screen. It waits for a
    // key: it is a mistake in a settings file, and it will happen every time
    // until it is fixed.
    if (say != NULL) {
        ui_message(&ed->ui_, &ed->scr_, (char*) say);
        scr_clear(&ed->scr_);
    }

    // The document is only drawn when there is something in it -- painting a
    // screenful of spaces over an already-cleared screen is 1800 bytes down the
    // VDP link for nothing. The cursor is drawn either way, which it was not:
    // starting AED with no file left no cursor on screen at all until the first
    // keystroke happened to repaint the row it was on.
    ed->banner_ = false;
    ed->keys_ = prog->keys;
    ed->leaving_ = false;
    if (tb_used(&ed->doc_->buf_) > 0) {
        cmd_show(ed);
    } else {
        // Nothing to show, so say what this is and where the commands are.
        // Only when no file was named: opening an empty file is a different
        // thing from starting with nothing, and someone who named a file has
        // already said what they came to do.
        if (fname == NULL && prog->banner != NULL) {
            prog->banner(&ed->ui_, &ed->scr_);
            ed->banner_ = true;
        }
        scr_show_cursor_ch(&ed->scr_, tb_peek(&ed->doc_->buf_));
    }


    // Last, so that no failure above has to take it back down again.
    keys_open();

    return ed;
}

void ed_destroy(editor* ed) {
    keys_close();
    tb_set_undo(&ed->home_.buf_, NULL);
    undo_destroy(&ed->home_.undo_);
    clip_destroy(&ed->clip_);
    ui_destroy(&ed->ui_);
    scr_destroy(&ed->scr_);
    tb_destroy(&ed->home_.buf_);
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
        if (!ed->doc_->selecting_) {
            ed->doc_->anchor_ = tb_tell(&ed->doc_->buf_);
            ed->doc_->selecting_ = true;
        }

        return SEL_EXTEND;
    }
    if (ed->doc_->selecting_) {
        // A key that puts something in the document or takes something out
        // replaces the selection rather than acting next to it. selecting_ is
        // left set so the caller can still see what to delete; deleting it is
        // what clears it.
        if (kc.flags & KC_EDITS) {
            return SEL_REPLACE;
        }
        ed->doc_->selecting_ = false;

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
    text_buffer* tb = &ed->doc_->buf_;
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
    text_buffer* buf = &ed->doc_->buf_;
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

void ed_set_cursor_flash(editor* ed, bool on) {
    ed->scr_.cursorFlash_ = on;
}

void ed_run(editor* ed) {
    for (;;) {
        // The document on screen, asked for on every pass: a program that
        // keeps more than one switches doc_ while handling a key, and a
        // footer drawn from the one this loop started on went on naming that
        // file, at that position, after the switch.
        text_buffer* buf = &ed->doc_->buf_;

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
        // keys_poll drops those. So the footer comes back on the next key
        // pressed after the chord, not on the release itself.
        //
        // Reading them is a load through the sysvars pointer, not a call into
        // MOS, so asking costs nothing on the path this is protecting.
        if (ed_footer_wanted((char) getsysvar_keymods())) {
            scr_footer(&ed->scr_, tb_fname(buf), tb_changed(buf),
                       tb_xpos(buf), tb_ypos(buf));
        }

        // The VDP's flashing cursor, only for as long as the document waits.
        // Everything that answers a key -- a prompt, a modal, the paint after
        // an edit -- runs with it hidden again, so it is never left flashing
        // at the end of whatever was drawn last.
        if (ed->scr_.cursorFlash_) {
            scr_cursor_flash(&ed->scr_, true);
        }
        const key_press kp = ks_wait(ed->ui_.keys_);
        if (ed->scr_.cursorFlash_) {
            scr_cursor_flash(&ed->scr_, false);
        }
        if (!ed_handle(ed, ed_translate(ed->keys_, kp))) {
            break;
        }
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
    scr_clear_textarea(&ed->scr_, ed->scr_.v_->topY_, (char) (ed->scr_.v_->bottomY_ - 1));
    scr_show_cursor_ch(&ed->scr_, tb_peek(&ed->doc_->buf_));
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

static const key_binding ED_BINDINGS[] = {
    // With CTRL.
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
    LETTER(VK_c, VK_C, C, OWN, cmd_copy),
    LETTER(VK_g, VK_G, C, 0, cmd_goto),
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

const keymap ED_KEYS = {
    ED_BINDINGS, (int) (sizeof(ED_BINDINGS) / sizeof(ED_BINDINGS[0])), NULL,
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

    // Unsigned, because char is signed on the eZ80: an accented letter -- á
    // from a keyboard layout's dead key, 0xE1 in the VDP's Windows-1252 --
    // is a character to type, and as a signed char it is negative and was
    // dropped as though it were a control code.
    const char ctrl = kp.mods & MOD_CTRL;
    const unsigned char ch = (unsigned char) kp.ch;
    if (!ctrl && (ch == '\t' || (ch != 0x7F && ch >= 32))) {
        kc.flags |= KC_PUTC | KC_EDITS;

        return kc;
    }
    for (; km != NULL; km = km->next) {
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

            return kc;
        }
    }

    return kc;
}
