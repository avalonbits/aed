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

#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "app.h"
#include "cmd_ops.h"

#define SCR(ed) screen* scr = &ed->scr_
#define UI(ed) user_input* ui = &ed->ui_

/*
 * The settings, as the engine in config.c reads and writes them. Adding one
 * is a field in settings.h, a line here, and -- if a fresh file should show
 * it -- a line in ed_settings_render.
 */
const cfg_setting ED_SETTINGS[] = {
    { "editor",  "tab", CFG_INT, offsetof(ed_settings, tab_size), 0, -1 },
    { "colours", "fg",  CFG_INT, offsetof(ed_settings, fg), 0, -1 },
    { "colours", "bg",  CFG_INT, offsetof(ed_settings, bg), 0, -1 },
    { "vdp",     "ctrl_pause_frames", CFG_INT,
      offsetof(ed_settings, ctrl_pause), 0, -1 },
    { "editor",  "font", CFG_STR, offsetof(ed_settings, font), CFG_FONT_MAX,
      offsetof(ed_settings, font_none) },
};
_Static_assert(sizeof(ED_SETTINGS) / sizeof(ED_SETTINGS[0])
               == ED_SETTINGS_COUNT,
               "ED_SETTINGS_COUNT is how many settings there are");

/*
 * A fresh settings file. Only the numbers are written as values: the font is
 * the one setting the editor only ever reads, and it is shown as a commented
 * example rather than a value, because setting it is also the declaration
 * that the VDP has the font API -- which the editor cannot check. The header
 * and that example name the program, which is the only thing that differs
 * from one program's file to another's.
 */
int ed_settings_render(const void* values, const char* name,
                       const char* font_dir, char* buf, int max) {
    const ed_settings* cfg = (const ed_settings*) values;
    int at = cfg_put_text(buf, 0, max, "# ");
    at = cfg_put_text(buf, at, max, name);
    at = cfg_put_text(buf, at, max,
        " settings.\r\n"
        "#\r\n"
        "# An INI file: [section] headings, then name = value lines. "
        "Blank lines\r\n"
        "# are ignored and '#' or ';' starts a comment. "
        "Sections and settings ");
    at = cfg_put_text(buf, at, max, name);
    at = cfg_put_text(buf, at, max,
        "\r\n"
        "# does not recognise are skipped, so this file stays readable "
        "by older and\r\n"
        "# newer versions alike. Edit and restart ");
    at = cfg_put_text(buf, at, max, name);
    at = cfg_put_text(buf, at, max, " to apply.\r\n");
    at = cfg_put_text(buf, at, max,
        "\r\n[editor]\r\n"
        "# How wide a tab renders, in columns. 1 to 16.\r\n");
    at = cfg_put_setting(buf, at, max, "tab", cfg->tab_size);
    at = cfg_put_text(buf, at, max,
        "\r\n# A font to load at startup: a raw bitmap, 256 glyphs, 8 pixels wide,\r\n"
        "# one byte per row. Its height is the file size divided by 256, so a\r\n"
        "# 2304-byte file is 9 rows -- an 8-row font with a blank row added, which\r\n"
        "# separates the text lines without costing a column.\r\n"
        "#\r\n"
        "# Needs a VDP with the font API (Console8 2.8.0+). ");
    at = cfg_put_text(buf, at, max, name);
    at = cfg_put_text(buf, at, max,
        " cannot check, so\r\n"
        "# uncommenting this is what says yours has it.\r\n"
        "#font = ");
    at = cfg_put_text(buf, at, max, font_dir);
    at = cfg_put_text(buf, at, max, "/unscii8x9.bin\r\n");
    at = cfg_put_text(buf, at, max,
        "\r\n[colours]\r\n"
        "# Text and background colour, as Agon colour numbers. These were\r\n"
        "# taken from the colours your Agon was already using.\r\n");
    at = cfg_put_setting(buf, at, max, "fg", cfg->fg);
    at = cfg_put_setting(buf, at, max, "bg", cfg->bg);

    return at;
}

/*
 * A font was asked for and did not load: the file is missing, or it is not
 * a whole number of 256-byte rows, or this VDP has no font API. Whichever
 * it was, saying nothing leaves the stock font on screen and no reason for
 * it -- and the setting sits in a file edited by hand, so a typo in the
 * path is the likeliest cause and the least guessable.
 *
 * It waits for a key. That is an interruption at startup, which is the
 * point: it is a mistake in a settings file, and it will happen every time
 * until it is fixed.
 *
 * The message is built here and shown by ed_init_for once there is a prompt
 * row to show it on. Its buffer is static for the frame's sake, and holds the
 * message only until then.
 */
static const char* font_not_loaded(const char* font) {
    // Built by hand rather than with snprintf. This is the program's only
    // formatted print, and asking for it links nanoprintf: 4,994 bytes,
    // eight per cent of the binary, for one %s.
    static const char lead[] = "font not loaded: ";
    static char msg[CFG_FONT_MAX + sizeof(lead)];
    const int lead_n = (int) sizeof(lead) - 1;
    int n = (int) strlen(font);
    if (n > (int) sizeof(msg) - lead_n - 1) {
        n = (int) sizeof(msg) - lead_n - 1;
    }
    memcpy(msg, lead, (size_t) lead_n);
    memcpy(msg + lead_n, font, (size_t) n);
    msg[lead_n + n] = 0;

    return msg;
}

const char* ed_settings_apply(editor* ed, const cfg_schema* sc) {
    screen* scr = &ed->scr_;
    const app_context* app = app_get();

    // Settings are read once at startup. The setters clamp or reject out of
    // range values, so a bad number in the file falls back rather than
    // rejecting the file -- there is nowhere useful to report an error to.
    //
    // On first run there is no file. Write one holding what the editor is
    // starting with, including the colours it just measured off the Agon, so
    // the user has something to edit instead of a format to guess at.
    const char* say = NULL;

    ed_settings cfg;
    cfg_defaults(sc, &cfg);
    // Before anything reads them: a card written by an older version has the
    // settings under the old name, and this is the one run that moves them.
    const bool moved = cfg_migrate(app->cfg_old, app->cfg_path);

    if (cfg_load(sc, &cfg, app->cfg_path)) {
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
        if (cfg.font[0] != 0 && !scr_load_font(scr, cfg.font)) {
            say = font_not_loaded(cfg.font);
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
        cfg_save(sc, &cfg, app->cfg_path);
    }
    /*
     * And when the move could not finish, nothing is written at all. The old
     * file still holds the reader's settings and the next run will try again;
     * a fresh one written now would be found first from then on, and their
     * settings would sit in a file nothing reads.
     */

    return say;
}

// The settings, in the order they are shown. Each is edited in the way that
// suits it: a number is typed, the colours go to the picker that already
// exists, and a font is chosen from what is on the card.
// ctrl_pause_frames is deliberately not here. It is the one setting that can
// do harm to get wrong -- the sequence carrying it means something else on a
// VDP that does not know it, and one of the bytes after it clears the screen --
// and explaining that in a row of a list is more confusing than useful. It
// stays in the settings file for anyone who wants it; the README says what it
// is for.
typedef enum _setting_row {
    ROW_TAB = 0,
    ROW_COLOURS,
    ROW_FONT,
    ROW_COUNT,
} setting_row;

// Reads a number from the reader, between lo and hi. Returns false when they
// gave up or typed something that is not one, in which case nothing changes --
// a bad answer must not silently become zero.
static bool ask_number(user_input* ui, screen* scr, char* title, int cur,
                       int lo, int hi, int* out) {
    char prefill[8];
    int p = ui_put_num(prefill, 0, (int) sizeof(prefill) - 1, cur < 0 ? 0 : cur);
    prefill[p] = 0;

    char* buf = NULL;
    int sz = 0;
    if (ui_text(ui, scr, title, prefill, &buf, &sz) != YES_OPT || sz <= 0) {
        return false;
    }

    int v = 0;
    for (int i = 0; i < sz; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return false;
        }
        v = v * 10 + (buf[i] - '0');
        if (v > hi) {
            return false;
        }
    }
    if (v < lo) {
        return false;
    }
    *out = v;

    return true;
}

static RESPONSE settings_modal(user_input* ui, screen* scr,
                               const cfg_schema* sc, ed_settings* cfg) {
    // What the editor is using now, which is what the rows show until
    // something is changed. `cfg` itself is left holding only the changes: it
    // is written back with cfg_update, which copies through every setting it
    // is not told about, and telling it about one the reader never touched
    // would rewrite a line they had left alone.
    const int tab_now = scr_tab_size(scr);
    const int fg_now = scr_base_fg(scr);
    const int bg_now = scr_base_bg(scr);

    // The two CFG_FONT_MAX buffers here are static for the same reason the
    // directory walk's are: together they are 128 bytes, which is the whole ix
    // displacement, and everything past it in the frame then costs five
    // instructions an access. A modal is entered once at a time.
    static char font_now[CFG_FONT_MAX];
    const int fl = (int) strlen(cfg->font);
    memcpy(font_now, cfg->font, (size_t)(fl < CFG_FONT_MAX ? fl : CFG_FONT_MAX - 1));
    font_now[fl < CFG_FONT_MAX ? fl : CFG_FONT_MAX - 1] = 0;
    cfg_defaults(sc, cfg);

    const char top = scr->v_->topY_;
    const char bottom = ui->ypos_;
    const int width = scr->v_->cols_ < 255 ? scr->v_->cols_ : 255;
    static char line[256];
    int at = 0;
    bool changed = false;

    for (;;) {
        int tab = cfg->tab_size >= 0 ? cfg->tab_size : tab_now;
        int fg = cfg->fg >= 0 ? cfg->fg : fg_now;
        int bg = cfg->bg >= 0 ? cfg->bg : bg_now;
        // Three states, not two: a path that was chosen, no font asked for,
        // and nothing said either way -- in which case the row shows what the
        // file already had. Reading only the path conflated the middle one
        // with the last, so choosing "none" left the old font on the row and
        // looked as though nothing had happened.
        const char* font = cfg->font_none
                           ? ""
                           : (cfg->font[0] != 0 ? cfg->font : font_now);

        char y = ui_modal_title(scr, top, width, "  SETTINGS");
        int k = 0;

        for (int i = 0; i < ROW_COUNT && y < bottom; i++) {
            k = ui_put_at(line, 0, width, i == at ? "  > " : "    ");
            switch (i) {
                case ROW_TAB:
                    k = ui_put_at(line, k, width, "tab width");
                    k = ui_pad_to(line, k, width, 24);
                    k = ui_put_num(line, k, width, tab);
                    break;
                case ROW_COLOURS:
                    k = ui_put_at(line, k, width, "colours");
                    k = ui_pad_to(line, k, width, 24);
                    k = ui_put_at(line, k, width, "text ");
                    k = ui_put_num(line, k, width, fg);
                    k = ui_put_at(line, k, width, " on ");
                    k = ui_put_num(line, k, width, bg);
                    break;
                case ROW_FONT:
                    k = ui_put_at(line, k, width, "font");
                    k = ui_pad_to(line, k, width, 24);
                    k = ui_put_at(line, k, width,
                               font[0] != 0 ? font : "(the machine's own)");
                    break;
                default:
                    break;
            }
            scr_write_line(scr, y++, line, k);
        }
        ui_modal_fill(scr, y, bottom,
                   "  UP/DOWN to choose, RETURN to change, ESC to close", 51);

        const key_press kp = ks_wait(ui->keys_);
        const pick_key act = ui_modal_move(kp.vkey, &at, ROW_COUNT - 1);
        if (act == PICK_LEAVE) {
            return changed ? YES_OPT : CANCEL_OPT;
        }
        if (act != PICK_TAKE) {
            continue;
        }

        switch (at) {
            case ROW_TAB: {
                int v = 0;
                if (ask_number(ui, scr, "Tab width, 1 to 16:", tab, 1,
                               SCR_MAX_TAB_SIZE, &v)) {
                    cfg->tab_size = v;
                    scr_set_tab_size(scr, (char) v);
                    changed = true;
                }
            } break;
            case ROW_COLOURS:
                // The picker sets the screen's colours as it goes, so what it
                // leaves behind is the answer.
                if (ui_color_picker(ui, scr) == YES_OPT) {
                    cfg->fg = scr_base_fg(scr);
                    cfg->bg = scr_base_bg(scr);
                    changed = true;

                    // Repaint the whole screen, not just the rows this modal
                    // draws on. The title bar is not one of them, so it kept
                    // the colours it was drawn in and the new scheme appeared
                    // to have reached only the middle of the screen.
                    //
                    // scr_clear moves the cursor's row to the top of the text
                    // area as a side effect, and what put the document back
                    // reads that row to work out where the view was -- so it
                    // is saved across the clear here, the same as there.
                    const char currX = scr->v_->currX_;
                    const char currY = scr->v_->currY_;
                    scr_clear(scr);
                    scr->v_->currX_ = currX;
                    scr->v_->currY_ = currY;
                }
                break;
            case ROW_FONT: {
                static char picked[CFG_FONT_MAX];
                const RESPONSE got = ui_font_picker(ui, scr, picked, CFG_FONT_MAX);
                if (got == YES_OPT) {
                    const int n = (int) strlen(picked);
                    memcpy(cfg->font, picked, (size_t) n);
                    cfg->font[n] = 0;
                    cfg->font_none = false;
                    changed = true;
                } else if (got == NO_OPT) {
                    // Asked for no font at all, which is a different answer
                    // from saying nothing about it: the line is written out
                    // empty rather than left alone.
                    cfg->font[0] = 0;
                    cfg->font_none = true;
                    changed = true;
                }
            } break;
            default:
                break;
        }
    }
}

RESPONSE ed_settings_modal(user_input* ui, screen* scr, const cfg_schema* sc,
                           ed_settings* cfg) {
    // Laid out on the whole screen, like the header and footer, whichever view
    // is current: a program showing more than one view gets the settings across
    // the whole text area rather than squeezed into one pane.
    view* was = scr->v_;
    scr_set_view(scr, NULL);
    const RESPONSE got = settings_modal(ui, scr, sc, cfg);
    scr_set_view(scr, was);

    return got;
}

void ed_cmd_settings(editor* ed, const cfg_schema* sc) {
    SCR(ed);
    UI(ed);

    // Comes in holding what the settings file says, so the font row can show
    // the one in use, and goes out holding only what was changed.
    ed_settings cfg;
    cfg_defaults(sc, &cfg);
    cfg_load(sc, &cfg, app_get()->cfg_path);

    // A theme is chosen for the background it was written against, so the one
    // in force may be the wrong one by the time this modal closes.
    const char was_bg = scr_base_bg(scr);

    const RESPONSE ret = ed_settings_modal(ui, scr, sc, &cfg);

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
     * order cmd_restore_after_modal already relies on.
     */
    if (scr_base_bg(scr) != was_bg) {
        ed_pick_syntax(ed);
    }

    cmd_restore_after_modal(ed, moved);

    if (ret == YES_OPT) {
        // Only the changed settings are set, and cfg_update copies every other
        // line through as it found it -- comments, spacing, and anything a
        // later version understands and this one does not.
        cfg_update(sc, &cfg, app_get()->cfg_path);
    }
}
