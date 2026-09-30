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

#include "aed.h"

#include "app.h"
#include "cmd_ops.h"
#include "config.h"

#include <stdbool.h>
#include <string.h>

#define SCR(ed) screen* scr = &ed->scr_
#define UI(ed) user_input* ui = &ed->ui_

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

void aed_cmd_help(editor* ed) {
    SCR(ed);
    UI(ed);

    aed_help(ui, scr);

    // The help wrote over the document, and the view cannot put it back on its
    // own -- it has no access to the buffer.
    cmd_restore_after_modal(ed, false);
}

void aed_cmd_settings(editor* ed) {
    ed_cmd_settings(ed, &AED_CONFIG);
}

#define C MOD_CTRL

// Both cases of a letter: MOS reports the shifted one when SHIFT is held.
#define LETTER(lo, up, mods, flags, cmd) \
    { lo, mods, flags, cmd }, { up, mods, flags, cmd }

// AED's own keys: its files, leaving, and its help and settings. Everything
// else -- moving, editing, selecting, the clipboard, finding, undo -- is the
// editor's, in ED_KEYS, which these are put in front of.
static const key_binding AED_BINDINGS[] = {
    LETTER(VK_o, VK_O, C, 0, cmd_open),
    LETTER(VK_s, VK_S, C | MOD_ALT, 0, cmd_save_as),
    LETTER(VK_s, VK_S, C, 0, ed_cmd_save),
    LETTER(VK_q, VK_Q, C, 0, ed_cmd_quit),
    LETTER(VK_h, VK_H, C, 0, aed_cmd_help),
    LETTER(VK_e, VK_E, C, 0, aed_cmd_settings),
};

#undef LETTER
#undef C

const keymap AED_KEYS = {
    AED_BINDINGS, (int) (sizeof(AED_BINDINGS) / sizeof(AED_BINDINGS[0])),
    &ED_KEYS,
};


/*
 * AED's part of starting up: its settings, read and applied as soon as the
 * screen exists, with a font that would not load handed back to be reported
 * once there is a prompt to report it on. The rest of starting is
 * ed_init_for's.
 */
static const char* aed_apply_settings(editor* ed) {
    return ed_settings_apply(ed, &AED_CONFIG);
}

static const ed_program AED_PROGRAM = {
    &AED_APP, &AED_KEYS, aed_apply_settings, aed_banner,
    "AED: Another Text Editor",
};

editor* ed_init(editor* ed, int mem_kb, const char* fname) {
    return ed_init_for(ed, mem_kb, fname, &AED_PROGRAM);
}
