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

#ifndef _AED_CONFIG_H_
#define _AED_CONFIG_H_

#include <stdbool.h>

#include "config.h"

/*
 * AED's settings: which there are, and how a fresh /config/aed.ini reads. The
 * file format and the reading and rewriting of it are the engine's, in
 * config.h; this is the schema AED hands it.
 */

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
    // setting AED only ever reads it: see aed_config.c for why it is written as
    // a commented example rather than a value.
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

extern const cfg_schema AED_CONFIG;

#endif  // _AED_CONFIG_H_
