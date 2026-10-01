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

#ifndef _EDITOR_H_
#define _EDITOR_H_

#include <stdint.h>

#include "app.h"
#include "clipboard.h"
#include "document.h"
#include "cmd_ops.h"
#include "keys.h"
#include "vkey.h"
#include "screen.h"
#include "text_buffer.h"
#include "undo.h"
#include "user_input.h"

typedef struct _editor {
    screen scr_;
    user_input ui_;

    // The document being edited -- its text, undo log, grammar and selection
    // -- and the editor's own, which is where it starts. A program with more
    // than one document open keeps the others itself and points doc_ at each
    // in turn with ed_doc_show; AED only ever has home_.
    document* doc_;
    document home_;

    /*
     * The theme the document's grammar is coloured by. It belongs to the
     * screen rather than the document: it is chosen by the background, so a
     * change of background re-picks it for everything drawn there. Cleared,
     * with the grammar, when the file has no grammar, which puts the user's
     * own colours back.
     *
     * Held by value, and the editor is a static in main, so this is bss
     * rather than heap or stack.
     */
    theme theme_;

    // A startup banner is on the text area, waiting to be wiped. It cannot
    // just be painted over: a keystroke repaints one row, which would leave the
    // rest of it sitting behind the document. So the first key clears the whole
    // text area before anything else happens.
    bool banner_;

    // What the keys mean -- see keymap below -- and whether a command has
    // asked the loop to stop. ed_init_for sets keys_ from the program; left
    // NULL, no key is bound to anything and typing still types.
    const struct _keymap* keys_;
    bool leaving_;

    clipboard clip_;
    // What was last searched for, so CTRL+N and CTRL+P have something to
    // repeat. Session state, like the clipboard.
    char find_[64];
    int findsz_;
} editor;

/*
 * What a program brings to an editor: where its files are, its keys, and its
 * part of starting up.
 *
 * settings runs as soon as the screen exists and before anything is sized --
 * it may load a font, which changes how many rows there are. It returns a
 * message to report, or NULL for none; ed_init_for shows it once the prompts
 * exist and before the document is shown, and the text only has to last that
 * long. banner is what to show when the program starts with no file named and
 * nothing to show. Either may be NULL. The program keeps no state of its own
 * between them: an editor's is the editor's.
 */
typedef struct _ed_program {
    const app_context* app;
    const struct _keymap* keys;
    const char* (*settings)(editor* ed);
    void (*banner)(user_input* ui, screen* scr);
    // The title on the header row, or NULL for none; see scr_set_title.
    const char* title;
} ed_program;

/*
 * More than one document.
 *
 * An editor starts with its own, home_. A program that keeps others holds
 * them and a view for each, and makes one current at a time: every command
 * works on the current document, in the current view.
 *
 * ed_doc_open sets `doc` up holding `fname` -- or nothing, for NULL -- with
 * `mem_kb` of text and an undo log of its own, and makes it current in `v`,
 * which it lays out over the whole text area and shows from the top. On
 * anything but TB_OK nothing is changed: the document that was current still
 * is. No memory for another is TB_TOO_LARGE.
 *
 * ed_doc_show makes an open document current again in its view, as it was
 * left, and repaints. ed_doc_close gives back what ed_doc_open took, and
 * removes its scratch files; show another document first if it is current.
 */
tb_result ed_doc_open(editor* ed, document* doc, view* v, int mem_kb,
                      const char* fname);
void ed_doc_show(editor* ed, document* doc, view* v);

/*
 * Puts an open document's cursor at (line, x) -- x from 0, both clamped to
 * the text -- and sets `v` round it as a search or a jump would: the line
 * halfway down, or as low as the lines above it allow, and the view scrolled
 * sideways to show the column. Nothing is drawn, so a program restoring
 * several documents places each and then shows one, with one paint. Showing
 * the document with ed_doc_show then paints it where it was put.
 *
 * Placing the document on screen moves its cursor and scroll the same way,
 * and still draws nothing: the screen shows the old place until the caller
 * repaints -- ed_doc_show, or cmd_restore_after_modal.
 */
void ed_doc_place(editor* ed, document* doc, view* v, int line, int x);
/*
 * Puts the screen back after the program has handed it to something else --
 * a program it ran, say -- that may have changed the screen mode. A mode
 * change resets the VDP's colours, font and scroll protection even when the
 * mode is the same one, and the editor would otherwise go on drawing as if
 * they were still its own: a title bar in two sets of colours and a black
 * border round the text. Sends them again and repaints the whole screen.
 */
void ed_resume(editor* ed);
void ed_doc_close(document* doc);

// Sets up an editor for `prog`, with a document of `mem_kb` holding `fname` --
// or nothing, for NULL. Returns NULL, having said why, when it cannot.
editor* ed_init_for(editor* ed, int mem_kb, const char* fname,
                    const ed_program* prog);

/*
 * Chooses the grammar and theme for the document now in the buffer, by its
 * name and by the background in force.
 *
 * Called whenever either of those changes -- at startup, on CTRL+O, and when
 * the settings modal leaves a different background behind. A file no grammar
 * claims clears both and puts the user's own colours back, which is what makes
 * a theme a view of a document rather than a setting.
 */
void ed_pick_syntax(editor* ed);

// True for the keys that move the cursor without changing the document. Holding
// shift with one of these is what starts and extends a selection; anything else
// ends it.
bool ed_is_motion(VKey vkey);

// True for the modifier keys themselves. MOS reports the press of one as an
// event in its own right, before the key it modifies, so anything deciding what
// a keypress means has to skip them -- otherwise holding shift is itself a
// keystroke, and it arrives right in the middle of the selection it is making.
bool ed_is_modifier(VKey vkey);

// Takes an editor down, and its own document with it. Documents a program
// opened with ed_doc_open are the program's to close.
void ed_destroy(editor* ed);

void ed_run(editor* ed);

/*
 * Makes the cursor flash while the editor waits for a key, as the MOS prompt's
 * does, or holds it steady again. Steady is the default. Flashing is the VDP's
 * own cursor, shown over the editor's while the document waits for a key and
 * hidden again as soon as one arrives -- so a prompt or a modal, which run
 * while a key is being handled, never shows it somewhere it does not belong.
 * The settings' cursor_flash sets the same thing, so a program with settings
 * gets it from the file without calling this.
 */
void ed_set_cursor_flash(editor* ed, bool on);

/*
 * What a key means: the command it runs and what it says about itself.
 *
 * The flags are what the loop decides from before the command runs, which is
 * why they travel with the key rather than being asked of the command.
 */
#define KC_OWNS_SEL 0x01    // about the selection, so it does not end it
#define KC_EDITS    0x02    // changes the document, so it replaces a selection
#define KC_PUTC     0x04    // puts the key's character in the document

typedef struct _key_command {
    cmd_op cmd;
    key k;
    // Carried out of translation because whether a motion extends a selection
    // is decided above the command, not inside it: cmd_left does the same thing
    // either way.
    char mods;
    char flags;
} key_command;

/*
 * A front end's keys, as a table: the first binding whose key is the one
 * pressed, and whose modifiers are held, is what the key means.
 *
 * A binding with MOD_CTRL matches only while CTRL is down and one without it
 * only while CTRL is up; any other modifier it names must be held, and the
 * ones it does not name are ignored. So CTRL+ALT+S goes before CTRL+S, and
 * SHIFT+LEFT finds LEFT, which is how a selection is extended. Without CTRL, a
 * key that types a character puts it in the document before the table is
 * asked. A program adds its own keys by giving the editor its own table.
 */
typedef struct _key_binding {
    VKey vkey;
    char mods;
    char flags;
    cmd_op cmd;
} key_binding;

typedef struct _keymap {
    const key_binding* keys;
    int n;
    // Asked when nothing here matches, so a program can put a few keys of its
    // own in front of ED_KEYS rather than copying it. NULL ends the chain.
    const struct _keymap* next;
} keymap;

/*
 * The editor's own keys: moving by character, word, line, page and document,
 * editing, deleting a line, selecting, the clipboard, finding, going to a line,
 * undo and redo. A program puts its keys in front of these with its keymap's
 * `next` -- AED's are its files, leaving, help and settings -- and gets
 * everything else as AED has it.
 */
extern const keymap ED_KEYS;

// Saving, and leaving -- which asks about unsaved changes and, if the answer
// is to go, stops the loop. ED_KEYS binds neither: which keys save and quit is
// the program's choice. AED's are CTRL+S and CTRL+Q.
void ed_cmd_save(editor* ed);
void ed_cmd_quit(editor* ed);

// What a key means under `km` and the maps it chains to, the first binding
// that matches winning. Waits for nothing: the key is the caller's, so
// a program can read keys however it likes and still hand them here. This is
// where a chord that MOS reports perfectly well can still be lost, and nothing
// below it would notice.
key_command ed_translate(const keymap* km, key_press kp);

// Runs one key through the editor: the selection, the command, and the
// repaint afterwards. False once a command has asked to leave.
bool ed_handle(editor* ed, key_command kc);

// Whether the footer should be drawn, given the modifiers held right now.
//
// It is not, while both CTRL and SHIFT are down. The footer sits on the bottom
// row, so drawing it means moving the cursor off the text, writing, and moving
// back, and doing that between keystrokes stops the next one arriving -- which
// is exactly what breaks word-wise selection, the one thing that chord is for.
// The position it reports is also the least interesting then: it is mid-drag.
//
// The caller passes the modifiers MOS has now. That does not make the footer
// return on the release -- the event loop only turns when a key arrives, and a
// release is not one -- so it comes back on the next key after the chord.
bool ed_footer_wanted(char held);

// Takes the startup banner off the text area, if one is up. Called on every
// key: it is a no-op after the first. Separate from the loop so that what it
// does can be checked -- painting over the banner instead of clearing would
// leave most of it behind, and that is invisible until someone looks.
void ed_clear_banner(editor* ed);

typedef enum _sel_action {
    SEL_NONE = 0,   // there was no selection and there still is none
    SEL_EXTEND,     // one was started or is being extended
    SEL_DROP,       // one was in progress and this key ended it
    SEL_REPLACE,    // ...and the key changes the document, so it takes its place
} sel_action;

// Applies a keypress to the selection, before the command it names runs. The
// return value tells the caller whether anything needs repainting, which is why
// this is separate from running the command: the decision is made from the key
// and the answer is needed again afterwards.
sel_action ed_selection_for(editor* ed, key_command kc);

// Repaints whatever the selection changed, after the command has run. The
// "before" values are read before it: the cursor's screen row and column, the
// document line then at the top of the screen, and the horizontal scroll
// origin. Between them they say whether the view moved under the text, which
// decides how much has to be redrawn -- and when it did not move, x_before says
// which columns of the row changed, so only those need sending.
void ed_selection_repaint(editor* ed, sel_action act, char y_before,
                          char x_before, int top_before, int origin_before);

#endif  // _EDITOR_H_
