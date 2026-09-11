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

#include "bootfont.h"

#include <agon/mos.h>
#include <stdbool.h>
#include <string.h>

// The VDU sequence a font selection is made of: VDU 23, 0, &95, 0, id; flags.
#define VDU_SYS      23
#define VDU_SYS_EXT  0
#define VDU_FONT     0x95
#define FONT_SELECT  0

// 65535 is the system font, which is what "no font of our own" already means.
#define FONT_SYSTEM  65535

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Whether `s` begins with `word` and the word ends there. Case-insensitive: MOS
// does not care how a command is typed and neither should this.
static bool starts_with_word(const char* s, int len, const char* word) {
    int i = 0;
    for (; word[i] != 0; i++) {
        if (i >= len || lower(s[i]) != lower(word[i])) {
            return false;
        }
    }

    return i >= len || is_space(s[i]);
}

// One number from a VDU argument list, as MOS reads them: decimal, or hex with
// an 'H' suffix, and a ';' suffix meaning "this is sixteen bits" -- which this
// does not need to act on, because the value is the same either way.
//
// Returns false when the token is not a number, which ends the scan of a line:
// a VDU line with something unparseable in it is not one this understands.
static bool take_number(const char* s, int len, int* at, int* out) {
    int i = *at;
    while (i < len && (is_space(s[i]) || s[i] == ',')) {
        i++;
    }
    if (i >= len) {
        return false;
    }

    // Hex is marked by a suffix, so the base is not known until the end. Both
    // are accumulated and the suffix picks one.
    int dec = 0;
    int hex = 0;
    int digits = 0;
    bool dec_ok = true;
    for (; i < len; i++) {
        const char c = lower(s[i]);
        int v;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v = c - 'a' + 10;
            dec_ok = false;
        } else {
            break;
        }
        if (dec_ok) {
            dec = dec * 10 + v;
        }
        hex = hex * 16 + v;
        digits++;
        if (hex > 0xFFFFF) {
            return false;       // far past anything a buffer id can be
        }
    }
    if (digits == 0) {
        return false;
    }

    bool is_hex = false;
    if (i < len && lower(s[i]) == 'h') {
        is_hex = true;
        i++;
    }
    if (i < len && s[i] == ';') {
        i++;                    // sixteen bits; the value is unchanged
    }
    if (i < len && !is_space(s[i]) && s[i] != ',') {
        return false;           // trailing rubbish: not a plain number
    }
    if (!is_hex && !dec_ok) {
        return false;           // hex digits without the suffix
    }

    *out = is_hex ? hex : dec;
    *at = i;

    return true;
}

// `fontctl <id>`. `fontctl sys` selects the system font, which is the same as
// having selected nothing.
static bool scan_fontctl(const char* s, int len, int* buffer) {
    if (!starts_with_word(s, len, "fontctl")) {
        return false;
    }
    int at = 7;
    int id = 0;
    if (!take_number(s, len, &at, &id)) {
        *buffer = -1;           // `sys`, or an argument this does not follow
        return true;
    }
    *buffer = (id == FONT_SYSTEM) ? -1 : id;

    return true;
}

// `VDU 23 0 149 0 <id>;` -- the same selection written out by hand.
static bool scan_vdu(const char* s, int len, int* buffer) {
    if (!starts_with_word(s, len, "vdu")) {
        return false;
    }

    static const int WANT[] = {VDU_SYS, VDU_SYS_EXT, VDU_FONT, FONT_SELECT};
    int at = 3;
    for (int i = 0; i < 4; i++) {
        int v = 0;
        if (!take_number(s, len, &at, &v) || v != WANT[i]) {
            return false;       // some other VDU line; nothing to say about it
        }
    }
    int id = 0;
    if (!take_number(s, len, &at, &id)) {
        return false;
    }
    *buffer = (id == FONT_SYSTEM) ? -1 : id;

    return true;
}

int bootfont_scan(const char* text, int len) {
    if (text == NULL || len <= 0) {
        return -1;
    }

    int buffer = -1;
    int i = 0;
    while (i < len) {
        const int start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        int end = i;
        i++;

        // Leading whitespace, and MOS treats a leading '*' as whitespace too.
        int b = start;
        while (b < end && (is_space(text[b]) || text[b] == '*')) {
            b++;
        }
        while (end > b && is_space(text[end - 1])) {
            end--;
        }
        if (b >= end) {
            continue;
        }

        const char* line = text + b;
        const int llen = end - b;

        // The last selection is the one in force by the time AED runs, so each
        // one replaces what came before rather than stopping the scan.
        int found = -1;
        if (scan_fontctl(line, llen, &found) || scan_vdu(line, llen, &found)) {
            buffer = found;
        }
    }

    return buffer;
}

int bootfont_read(const char* path) {
    if (path == NULL) {
        return -1;
    }

    char fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        return -1;              // no boot script is the ordinary case
    }

    static char buf[BOOTFONT_MAX];
    const int n = (int) mos_fread(fh, buf, BOOTFONT_MAX);
    mos_fclose(fh);

    return bootfont_scan(buf, n);
}
