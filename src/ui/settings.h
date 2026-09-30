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

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <stdbool.h>

#include "config.h"
#include "editor.h"
#include "user_input.h"

/*
 * The editor's settings -- tab width, colours, a font, the VDP's pause on a
 * wrapped line -- for any program built on the editor: which there are, how a
 * fresh file reads, applying them at startup, and the screen that changes
 * them. The file is the program's own, app_get()->cfg_path; the format and the
 * reading and rewriting of it are the engine's, in config.h.
 *
 * A program makes a cfg_schema from ED_SETTINGS and a render that calls
 * ed_settings_render with its name, and hands that schema to the rest:
 *
 *     static int render(const void* v, char* buf, int max) {
 *         return ed_settings_render(v, "AED", CFG_DIR "/aed", buf, max);
 *     }
 *     const cfg_schema AED_CONFIG = { ED_SETTINGS, ED_SETTINGS_COUNT, render };
 */

// Longest font path the settings file can name. A font lives beside the other
// per-application files, so `/config/aed/unscii8x9.bin` is the shape of it;
// this leaves room for a deeper directory without inviting a path that no
// longer fits on the footer.
#define CFG_FONT_MAX 64

typedef struct _ed_settings {
    // Negative means "not set in the file", so the caller keeps its own value.
    int tab_size;
    int fg;
    int bg;
    // Frames the VDP pauses for on a line wrap while CTRL is held. It defaults
    // to 3, which makes CTRL with an arrow key feel sluggish once a line
    // reaches the right-hand edge. Setting it to 0 turns that off.
    //
    // Off by default because the VDU sequence that sets it does not exist on
    // older VDPs, and one that does not know it reads the four bytes that
    // follow as commands -- one of which clears the screen.
    int ctrl_pause;

    // Path to a font file to load at startup, or an empty string for "not set".
    // Unlike every other setting this one is a string, and unlike every other
    // setting the editor only ever reads it: see ed_settings_render for why it
    // is written as a commented example rather than a value.
    //
    // Setting it is also the declaration that the VDP is new enough for the
    // font API (Console8 2.8.0). MOS cannot report the VDP version, so there is
    // nothing else to gate on -- the same shape ctrl_pause_frames took.
    char font[CFG_FONT_MAX];

    // Asking for no font at all, which is not the same as saying nothing about
    // it. An empty `font` means "this version has nothing to say, leave the
    // line as it is"; this means "write the line out empty", which is how the
    // file says no font -- cfg_parse ignores a setting with no value.
    bool font_none;
} ed_settings;

// The settings, for a program's cfg_schema.
extern const cfg_setting ED_SETTINGS[];
#define ED_SETTINGS_COUNT 5

// A fresh settings file for `values`, an ed_settings, into buf: a header
// naming the program as `name`, then each section with what it is for. The
// font is shown as a commented example in `font_dir`, which should be the
// program's app_context.font_dir: it is passed rather than read from the app
// so that a render needs nothing set up to run -- a schema's render takes no
// editor, and the golden test renders with no app at all. For a schema's
// render.
int ed_settings_render(const void* values, const char* name,
                       const char* font_dir, char* buf, int max);

/*
 * The settings, read from the program's file and applied as soon as the
 * screen exists: call it from ed_program.settings. On first run there is no
 * file, and one is written holding what the editor is starting with,
 * including the colours measured off the Agon, so there is something to edit
 * instead of a format to guess at. A font that would not load is handed back
 * as a message for ed_init_for to show once there is a prompt.
 */
const char* ed_settings_apply(editor* ed, const cfg_schema* sc);

// Shows the settings, lets them be changed, applies them, puts the document
// back, and writes the file when any were changed.
void ed_cmd_settings(editor* ed, const cfg_schema* sc);

// The settings, editable. Draws over the whole text area; the caller puts the
// document back. Returns YES_OPT when something was changed and is worth
// writing to the file.
//
// `cfg` comes in holding what the file says and goes out holding the changes
// -- and only the changes: everything else stays unset, so writing it back
// with cfg_update cannot invent a setting the reader never asked for.
RESPONSE ed_settings_modal(user_input* ui, screen* scr, const cfg_schema* sc,
                           ed_settings* cfg);

#endif  // _SETTINGS_H_
