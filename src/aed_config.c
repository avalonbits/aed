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

int aed_run_render(char* buf, int at, int max) {
    at = cfg_put_text(buf, at, max,
        "\r\n[run]\r\n"
        "# What CTRL+R runs, through hub, for each kind of file: %f is the file,\r\n"
        "# %b the file without its extension, %e the error file. c and asm build\r\n"
        "# it, and %b is run after; bas runs the file itself. The build has to\r\n"
        "# write its errors to %e as file:line:column: text for the cursor to go\r\n"
        "# to the first. Leave one out to use the command shown here.\r\n");
    at = cfg_put_text(buf, at, max, "c = " AED_RUN_C "\r\n");
    at = cfg_put_text(buf, at, max, "asm = " AED_RUN_ASM "\r\n");
    at = cfg_put_text(buf, at, max, "bas = " AED_RUN_BAS "\r\n");

    return at;
}

// A fresh aed.ini: the editor's file, naming AED, with the example font among
// AED's own -- and then what CTRL+R runs.
static int render(const void* values, char* buf, int max) {
    const int at = ed_settings_render(values, "AED", CFG_DIR "/aed", buf, max);

    return aed_run_render(buf, at, max);
}

const cfg_schema AED_CONFIG = {
    ED_SETTINGS, ED_SETTINGS_COUNT, render,
};

static const cfg_setting RUN_SETTINGS[] = {
    { "run", "c",   CFG_STR, offsetof(aed_run_config, c),    AED_RUN_MAX, -1 },
    { "run", "asm", CFG_STR, offsetof(aed_run_config, asm_), AED_RUN_MAX, -1 },
    { "run", "bas", CFG_STR, offsetof(aed_run_config, bas),  AED_RUN_MAX, -1 },
};

// Only ever read: the section is written as part of AED_CONFIG's fresh file.
static int run_render(const void* values, char* buf, int max) {
    (void) values;

    return aed_run_render(buf, 0, max);
}

const cfg_schema AED_RUN_CONFIG = {
    RUN_SETTINGS, (int) (sizeof(RUN_SETTINGS) / sizeof(RUN_SETTINGS[0])), run_render,
};
