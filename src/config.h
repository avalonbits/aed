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
// file takes a /config/<name>/ directory as well -- AED keeps its grammars,
// themes and fonts in one -- so the loader takes the path rather than assuming
// it.
#define CFG_DIR  "/config"

// The file is an INI file: [section] headings, then `name = value` lines, with
// '#' or ';' starting a comment. Nothing here needs the format's full
// generality; it was chosen because it is one people already know how to edit
// by hand and one a program can rewrite without guessing. Sections are what
// make room for growth -- syntax highlighting will want names like `fg` that
// mean something different from the ones in [colours].

/*
 * What a program's settings are: a table of them, each an INI section and name
 * and where its value lives in the program's own settings struct, and how a
 * fresh file reads.
 *
 * The engine below reads, merges and writes against a schema and a pointer to
 * that struct, so the file format, the parsing and the careful rewriting are
 * shared, and which settings there are -- and the comments a new file carries
 * -- are each program's. AED's is in aed_config.h.
 */
typedef enum _cfg_kind {
    CFG_INT,    // an int: negative means "not set"
    CFG_STR,    // text, in a char array of `size` bytes: empty means "not set"
} cfg_kind;

typedef struct _cfg_setting {
    const char* section;
    const char* name;
    cfg_kind kind;
    // Where the value is in the settings struct, as offsetof gives it.
    int at;
    // CFG_STR only: the array's size, and the offset of a bool that asks for
    // the setting to be written out empty -- "set, to nothing", which is not
    // the same as not set -- or -1 for a setting that cannot ask that.
    int size;
    int none;
} cfg_setting;

typedef struct _cfg_schema {
    const cfg_setting* settings;
    int n;
    // Writes a fresh file for `values` into `buf`: the program's layout and
    // comments, built with the cfg_put_* helpers. Returns the length, or a
    // negative number if it would not fit.
    int (*render)(const void* values, char* buf, int max);
} cfg_schema;

// The most settings a schema can have.
#define CFG_SETTINGS_MAX 16

// Every setting in `values` cleared to "not set".
void cfg_defaults(const cfg_schema* sc, void* values);

/*
 * Brings a card written by an older version up to date, once, before anything
 * reads the settings: `old` is what the settings file used to be called and
 * `path` what it is called now. Each program names its own -- see app.h.
 *
 * Does nothing when the settings file is already there, and nothing when
 * neither file is -- which is a first run -- or when `old` is NULL, for a
 * program whose file never had another name. Otherwise the old file is copied
 * to the new name and removed. A copy that fails leaves the old file exactly
 * where it was and takes the half-written new one away, so the next run tries
 * again rather than reading a truncated file.
 *
 * Returns false only in that last case: an old settings file is still waiting
 * to be moved. The caller must not write a fresh one while that is true. A
 * card that is full fails the copy, and a fresh file written afterwards would
 * be found by every later run -- which would report the move as done and
 * leave the reader's real settings sitting in the old file, unread, for good.
 */
bool cfg_migrate(const char* old, const char* path);

// Reads `path` into `values`. A missing or unreadable file is not an error:
// the settings simply stay unset. Unknown sections and names are ignored so
// that a file written for a later version still loads in this one. Section and
// setting names are matched without regard to case.
bool cfg_load(const cfg_schema* sc, void* values, const char* path);

// Parses config text directly. Exposed for tests, and so the file reading and
// the parsing can be checked separately.
void cfg_parse(const cfg_schema* sc, void* values, const char* text, int len);

// Writes `values` to `path` as a fresh file, creating the directory if needed.
// Used on first run to leave a commented file holding the settings the program
// started with, so there is something to edit rather than a format to guess at.
bool cfg_save(const cfg_schema* sc, const void* values, const char* path);

// Rewrites `path`, changing only the settings that are set in `values` and
// leaving every other line exactly as it was -- comments, blank lines, spacing,
// and settings this version does not understand. The file is meant to be
// edited by hand, so saving a colour change must not reformat it or discard
// notes. A setting whose section is absent gets that heading written for it.
// Falls back to writing a fresh file when there is nothing to merge into, and
// refuses rather than rewriting a file too large to hold in memory whole --
// writing back a partial read would truncate away everything past it.
bool cfg_update(const cfg_schema* sc, const void* values, const char* path);

// Renders a fresh file into `buf` through the schema's render. Returns the
// length written, or 0 if it would not fit. Separated from the file writing so
// it can be checked directly.
int cfg_render(const cfg_schema* sc, const void* values, char* buf, int max);

// For a schema's render. Each appends to `out` at `at` and returns where it
// stopped, or -1 once anything has not fitted -- which every later call passes
// on, so a render can make its calls in a row and check once at the end.
int cfg_put_text(char* out, int at, int max, const char* text);
// "name = value\r\n". CRLF because that is what the editor writes into text
// files, and this one is meant to be opened in it.
int cfg_put_setting(char* out, int at, int max, const char* name, int value);

#endif  // _CONFIG_H_
