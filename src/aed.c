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

void cmd_help(editor* ed) {
    SCR(ed);
    UI(ed);

    ui_help(ui, scr);

    // The help wrote over the document, and the view cannot put it back on its
    // own -- it has no access to the buffer.
    restore_after_modal(ed, false);
}

void cmd_settings(editor* ed) {
    SCR(ed);
    UI(ed);

    // Comes in holding what the settings file says, so the font row can show
    // the one in use, and goes out holding only what was changed.
    config cfg;
    cfg_defaults(&AED_CONFIG, &cfg);
    cfg_load(&AED_CONFIG, &cfg, app_get()->cfg_path);

    // A theme is chosen for the background it was written against, so the one
    // in force may be the wrong one by the time this modal closes.
    const char was_bg = scr_base_bg(scr);

    const RESPONSE ret = ui_settings(ui, scr, &cfg);

    // A font changes the cell size, and with it the number of rows and where
    // the footer sits. Everything below is laid out from those, so the font
    // goes in first and the screen is rebuilt from what it leaves behind.
    bool moved = false;
    if (ret == YES_OPT && (cfg.font[0] != 0 || cfg.font_none)) {
        if (cfg.font[0] != 0) {
            scr_load_font(scr, cfg.font);
        } else {
            scr_system_font(scr);
        }

        // The prompt row moved with the geometry; ui_ places everything it
        // draws from it, so a prompt left on the old bottom row would land in
        // the middle of the document.
        ui_resize(ui, scr->v_->bottomY_, scr->v_->cols_);

        // Fewer rows than before can leave the cursor past the bottom. Pulling
        // it back to the last text row keeps it somewhere the screen has, and
        // refresh_screen re-anchors the view from wherever it ends up.
        moved = true;
    }

    /*
     * The background the reader just picked chooses the theme, exactly as the
     * background at startup does: a colour that reads well on black is
     * unreadable on white, and a background no theme covers means painting
     * plainly. Done here rather than in the picker because the rule lives in
     * ed_pick_syntax, which knows the document as well as the background.
     *
     * Before the screen goes back, so the repaint below draws in whatever the
     * new background calls for. ed_pick_syntax winds the model back to the top
     * of the document and refresh_screen sets it from the view, which is the
     * order restore_after_modal already relies on.
     */
    if (scr_base_bg(scr) != was_bg) {
        ed_pick_syntax(ed);
    }

    restore_after_modal(ed, moved);

    if (ret == YES_OPT) {
        // Only the changed settings are set, and cfg_update copies every other
        // line through as it found it -- comments, spacing, and anything a
        // later version understands and this one does not.
        cfg_update(&AED_CONFIG, &cfg, app_get()->cfg_path);
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
    AED_BINDINGS, (int) (sizeof(AED_BINDINGS) / sizeof(AED_BINDINGS[0])), NULL,
};

