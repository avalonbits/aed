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

typedef struct _config {
    // Negative means "not set in the file", so the caller keeps its own value.
    int tab_size;
    int fg;
    int bg;
} config;

// Every field cleared to "not set".
void cfg_defaults(config* cfg);

// Reads `path` into `cfg`. A missing or unreadable file is not an error: the
// settings simply stay unset. Unknown keys are ignored so that a file written
// for a later version still loads.
bool cfg_load(config* cfg, const char* path);

// Parses config text directly. Exposed for tests, and so the file reading and
// the parsing can be checked separately.
void cfg_parse(config* cfg, const char* text, int len);

// Writes `cfg` to `path`, creating the directory if needed. Used on first run to
// leave a commented file holding the settings AED started with, so there is
// something to edit rather than a format to guess at.
bool cfg_save(const config* cfg, const char* path);

// Renders the file contents into `buf`. Returns the length written, or 0 if it
// would not fit. Separated from the file writing so it can be checked directly.
int cfg_render(const config* cfg, char* buf, int max);

#endif  // _CONFIG_H_
