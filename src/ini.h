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

/*
 * Reading an INI file, a line at a time.
 *
 * Extracted from config.c when a second thing needed it: a theme is an INI file
 * too, and so is a syntax grammar. The settings file is still the only thing
 * that knows what any particular name *means* -- this just says what shape a
 * line is.
 *
 * Nothing here allocates or keeps state. A line points into the text it was
 * read from, so the caller owns that for as long as it uses the result.
 */
#ifndef _INI_H_
#define _INI_H_

#include <stdbool.h>

typedef enum _line_kind {
    LINE_OTHER,     // blank, comment, or nothing we recognise
    LINE_SECTION,   // [name]
    LINE_SETTING,   // name = value
} line_kind;

typedef struct _line {
    line_kind kind;
    int eq;             // index of '=' within the line, for LINE_SETTING
    int cut;            // where a comment starts, or the line end
    const char* name;   // section or setting name
    int namelen;
    const char* value;  // trimmed value text, for LINE_SETTING
    int valuelen;
} line;

// Dissects s[start, end). `kind` says which of the other fields mean anything.
line ini_read_line(const char* s, int start, int end);

// Names are matched without regard to case, which is what INI readers
// conventionally do and one less thing to get wrong by hand.
bool ini_name_is(const char* s, int len, const char* name);

// A non-negative decimal. False when there are no digits or it is too big, so
// a malformed value leaves its setting alone rather than becoming zero.
bool ini_parse_number(const char* s, int len, int* out);

#endif  // _INI_H_
