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

#include "ini.h"

#include <string.h>

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

// ';' as well as '#': both are conventional in INI files and a hand-edited file
// should accept whichever the writer reaches for.
static bool is_comment(char c) {
    return c == '#' || c == ';';
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Section and setting names are matched without regard to case, which is what
// INI readers conventionally do and one less thing to get wrong by hand.
bool ini_name_is(const char* s, int len, const char* name) {
    if (name == NULL || len != (int) strlen(name)) {
        return false;
    }
    for (int i = 0; i < len; i++) {
        if (lower(s[i]) != lower(name[i])) {
            return false;
        }
    }

    return true;
}

// Reads a non-negative decimal number. Returns false when there are no digits,
// so a malformed value leaves the setting alone rather than becoming zero.
bool ini_parse_number(const char* s, int len, int* out) {
    int v = 0;
    int digits = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = (v * 10) + (s[i] - '0');
        digits++;
        if (v > 9999) {
            return false;
        }
    }
    if (digits == 0) {
        return false;
    }
    *out = v;

    return true;
}


line ini_read_line(const char* s, int start, int end) {
    line ln;
    ln.kind = LINE_OTHER;
    ln.eq = -1;
    ln.name = NULL;
    ln.namelen = 0;
    ln.value = NULL;
    ln.valuelen = 0;

    // A comment marker inside quotes is not one. Without this a value cannot
    // contain a ';' or a '#' at all, and both are wanted: a syntax grammar has
    // to be able to say that ';' begins a comment in the language it describes,
    // which is the exact character this reader would otherwise eat.
    int cut = end;
    char quote = 0;
    for (int c = start; c < end; c++) {
        if (quote != 0) {
            if (s[c] == quote) {
                quote = 0;
            }
            continue;
        }
        if (s[c] == '"' || s[c] == '\'') {
            quote = s[c];
            continue;
        }
        if (is_comment(s[c])) {
            cut = c;
            break;
        }
    }
    ln.cut = cut;

    int b = start;
    while (b < cut && is_space(s[b])) {
        b++;
    }
    int e = cut;
    while (e > b && is_space(s[e - 1])) {
        e--;
    }
    if (b == e) {
        return ln;
    }

    if (s[b] == '[') {
        int close = e;
        while (close > b && s[close - 1] != ']') {
            close--;
        }
        if (close <= b + 1) {
            return ln;   // no closing bracket: not a section
        }
        int ns = b + 1;
        int ne = close - 1;
        while (ns < ne && is_space(s[ns])) {
            ns++;
        }
        while (ne > ns && is_space(s[ne - 1])) {
            ne--;
        }
        ln.kind = LINE_SECTION;
        ln.name = s + ns;
        ln.namelen = ne - ns;

        return ln;
    }

    int eq = b;
    while (eq < cut && s[eq] != '=') {
        eq++;
    }
    if (eq >= cut) {
        return ln;   // no '=': not a setting
    }

    int ke = eq;
    while (ke > b && is_space(s[ke - 1])) {
        ke--;
    }
    int vs = eq + 1;
    while (vs < cut && is_space(s[vs])) {
        vs++;
    }
    int ve = cut;
    while (ve > vs && is_space(s[ve - 1])) {
        ve--;
    }

    ln.kind = LINE_SETTING;
    ln.eq = eq;
    ln.name = s + b;
    ln.namelen = ke - b;
    ln.value = s + vs;
    ln.valuelen = ve - vs;

    return ln;
}
