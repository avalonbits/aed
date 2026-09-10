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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdbool.h>

// Settings live in /config, alongside /bin and /mos rather than inside them:
// /bin is for executables. One file per application keeps the convention small
// enough for other applications to follow. An application that outgrows a single
// file can take a /config/<name>/ directory instead -- AED will want one for
// syntax definitions -- so the loader takes the path rather than assuming it.
#define CFG_DIR  "/config"
#define CFG_PATH CFG_DIR "/aed.cfg"

// The file is an INI file: [section] headings, then `name = value` lines, with
// '#' or ';' starting a comment. Nothing here needs the format's full
// generality; it was chosen because it is one people already know how to edit
// by hand and one a program can rewrite without guessing. Sections are what
// make room for growth -- syntax highlighting will want names like `fg` that
// mean something different from the ones in [colours].

// Longest font path the settings file can name. A font lives beside the other
// per-application files, so `/config/aed/unscii8x9.bin` is the shape of it;
// this leaves room for a deeper directory without inviting a path that no
// longer fits on the footer.
#define CFG_FONT_MAX 64

typedef struct _config {
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
    // setting AED only ever reads it: see cfg_render for why it is written as a
    // commented example rather than a value.
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
} config;

// Every field cleared to "not set".
void cfg_defaults(config* cfg);

// Reads `path` into `cfg`. A missing or unreadable file is not an error: the
// settings simply stay unset. Unknown sections and names are ignored so that a
// file written for a later version still loads in this one. Section and setting
// names are matched without regard to case.
bool cfg_load(config* cfg, const char* path);

// Parses config text directly. Exposed for tests, and so the file reading and
// the parsing can be checked separately.
void cfg_parse(config* cfg, const char* text, int len);

// Writes `cfg` to `path`, creating the directory if needed. Used on first run to
// leave a commented file holding the settings AED started with, so there is
// something to edit rather than a format to guess at.
bool cfg_save(const config* cfg, const char* path);

// Rewrites `path`, changing only the settings that are set in `cfg` and leaving
// every other line exactly as it was -- comments, blank lines, spacing, and
// settings this version does not understand. The file is meant to be edited by
// hand, so saving a colour change must not reformat it or discard notes. A
// setting whose section is absent gets that heading written for it. Falls back
// to writing a fresh file when there is nothing to merge into, and refuses
// rather than rewriting a file too large to hold in memory whole -- writing back
// a partial read would truncate away everything past it.
bool cfg_update(const config* cfg, const char* path);

// Renders the file contents into `buf`. Returns the length written, or 0 if it
// would not fit. Separated from the file writing so it can be checked directly.
int cfg_render(const config* cfg, char* buf, int max);

#endif  // _CONFIG_H_
