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

#include "aed_config.h"

#include <stddef.h>

/*
 * AED's settings, as the engine in config.c reads and writes them. Adding one
 * is a field in aed_config.h, a line here, and -- if a fresh file should show
 * it -- a line in render.
 */
static const cfg_setting SETTINGS[] = {
    { "editor",  "tab", CFG_INT, offsetof(config, tab_size), 0, -1 },
    { "colours", "fg",  CFG_INT, offsetof(config, fg), 0, -1 },
    { "colours", "bg",  CFG_INT, offsetof(config, bg), 0, -1 },
    { "vdp",     "ctrl_pause_frames", CFG_INT,
      offsetof(config, ctrl_pause), 0, -1 },
    { "editor",  "font", CFG_STR, offsetof(config, font), CFG_FONT_MAX,
      offsetof(config, font_none) },
};

/*
 * A fresh settings file. Only the numbers are written as values: the font is
 * the one setting AED only ever reads, and it is shown as a commented example
 * rather than a value, because setting it is also the declaration that the
 * VDP has the font API -- which AED cannot check.
 */
static const char* HEADER =
    "# AED settings.\r\n"
    "#\r\n"
    "# An INI file: [section] headings, then name = value lines. Blank lines\r\n"
    "# are ignored and '#' or ';' starts a comment. Sections and settings AED\r\n"
    "# does not recognise are skipped, so this file stays readable by older and\r\n"
    "# newer versions alike. Edit and restart AED to apply.\r\n";

static int render(const void* values, char* buf, int max) {
    const config* cfg = (const config*) values;
    int at = cfg_put_text(buf, 0, max, HEADER);
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
        "# Needs a VDP with the font API (Console8 2.8.0+). AED cannot check, so\r\n"
        "# uncommenting this is what says yours has it.\r\n"
        "#font = /config/aed/unscii8x9.bin\r\n");
    at = cfg_put_text(buf, at, max,
        "\r\n[colours]\r\n"
        "# Text and background colour, as Agon colour numbers. These were\r\n"
        "# taken from the colours your Agon was already using.\r\n");
    at = cfg_put_setting(buf, at, max, "fg", cfg->fg);
    at = cfg_put_setting(buf, at, max, "bg", cfg->bg);

    return at;
}

const cfg_schema AED_CONFIG = {
    SETTINGS, (int) (sizeof(SETTINGS) / sizeof(SETTINGS[0])), render,
};
