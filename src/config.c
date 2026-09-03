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

#include "config.h"

#include <agon/mos.h>
#include <string.h>

// A settings file is a handful of short lines; reading it whole costs less than
// buffering, and anything longer is not a settings file.
#define CFG_MAX 512

void cfg_defaults(config* cfg) {
    cfg->tab_size = -1;
    cfg->fg = -1;
    cfg->bg = -1;
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

// Reads a non-negative decimal number. Returns false when there are no digits,
// so a malformed value leaves the setting alone rather than becoming zero.
static bool parse_number(const char* s, int len, int* out) {
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

static bool key_is(const char* k, int klen, const char* name) {
    return klen == (int) strlen(name) && memcmp(k, name, (size_t) klen) == 0;
}

void cfg_parse(config* cfg, const char* text, int len) {
    if (text == NULL || len <= 0) {
        return;
    }

    int i = 0;
    while (i < len) {
        // One line, however it is terminated.
        int start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        int end = i;
        i++;   // step over the newline

        // A '#' starts a comment, whether it opens the line or follows a
        // setting: the file is hand-edited, so `tab = 8  # columns` should work.
        for (int c = start; c < end; c++) {
            if (text[c] == '#') {
                end = c;
                break;
            }
        }
        while (start < end && is_space(text[start])) {
            start++;
        }
        while (end > start && is_space(text[end - 1])) {
            end--;
        }
        if (start == end) {
            continue;   // blank, or nothing left once the comment is removed
        }

        // key = value
        int eq = start;
        while (eq < end && text[eq] != '=') {
            eq++;
        }
        if (eq == end) {
            continue;   // no '=', not a setting
        }

        int kend = eq;
        while (kend > start && is_space(text[kend - 1])) {
            kend--;
        }
        int vstart = eq + 1;
        while (vstart < end && is_space(text[vstart])) {
            vstart++;
        }

        const char* key = text + start;
        const int klen = kend - start;
        const char* val = text + vstart;
        const int vlen = end - vstart;

        int n = 0;
        if (key_is(key, klen, "tab") && parse_number(val, vlen, &n)) {
            cfg->tab_size = n;
        } else if (key_is(key, klen, "fg") && parse_number(val, vlen, &n)) {
            cfg->fg = n;
        } else if (key_is(key, klen, "bg") && parse_number(val, vlen, &n)) {
            cfg->bg = n;
        }
        // Unknown keys are ignored on purpose: a file written by a later
        // version has to keep working here.
    }
}

// Appends "name = value\r\n". CRLF because that is what the editor writes into
// text files, and this one is meant to be opened in it.
static int put_setting(char* buf, int at, int max, const char* name, int value) {
    const int nlen = (int) strlen(name);
    char digits[8];
    int dn = 0;
    if (value == 0) {
        digits[dn++] = '0';
    }
    for (int v = value; v > 0; v /= 10) {
        digits[dn++] = (char)('0' + (v % 10));
    }
    if (at + nlen + 3 + dn + 2 > max) {
        return -1;
    }

    memcpy(buf + at, name, (size_t) nlen);
    at += nlen;
    buf[at++] = ' ';
    buf[at++] = '=';
    buf[at++] = ' ';
    while (dn > 0) {
        buf[at++] = digits[--dn];
    }
    buf[at++] = '\r';
    buf[at++] = '\n';

    return at;
}

static int put_text(char* buf, int at, int max, const char* text) {
    const int n = (int) strlen(text);
    if (at + n > max) {
        return -1;
    }
    memcpy(buf + at, text, (size_t) n);

    return at + n;
}

int cfg_render(const config* cfg, char* buf, int max) {
    int at = put_text(buf, 0, max,
        "# AED settings.\r\n"
        "#\r\n"
        "# Blank lines are ignored and '#' starts a comment. Settings AED does\r\n"
        "# not recognise are skipped, so this file stays readable by older and\r\n"
        "# newer versions alike. Edit and restart AED to apply.\r\n"
        "\r\n"
        "# How wide a tab renders, in columns. 1 to 16.\r\n");
    if (at >= 0) {
        at = put_setting(buf, at, max, "tab", cfg->tab_size);
    }
    if (at >= 0) {
        at = put_text(buf, at, max,
            "\r\n"
            "# Text and background colour, as Agon colour numbers. These were\r\n"
            "# taken from the colours your Agon was already using.\r\n");
    }
    if (at >= 0) {
        at = put_setting(buf, at, max, "fg", cfg->fg);
    }
    if (at >= 0) {
        at = put_setting(buf, at, max, "bg", cfg->bg);
    }

    return at < 0 ? 0 : at;
}

bool cfg_save(const config* cfg, const char* path) {
    if (path == NULL) {
        return false;
    }

    static char buf[CFG_MAX];
    const int n = cfg_render(cfg, buf, CFG_MAX);
    if (n <= 0) {
        return false;
    }

    // The directory is very likely missing on first run. mos_mkdir failing
    // because it already exists is indistinguishable from any other failure
    // here, so just try the open and let that decide.
    mos_mkdir(CFG_DIR);

    char fh = mos_fopen(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;
    }
    mos_fwrite(fh, buf, (unsigned) n);
    mos_fclose(fh);

    return true;
}

bool cfg_load(config* cfg, const char* path) {
    if (path == NULL) {
        return false;
    }

    char fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        return false;   // no settings file is the normal case
    }

    static char buf[CFG_MAX];
    const int n = (int) mos_fread(fh, buf, CFG_MAX);
    mos_fclose(fh);
    if (n <= 0) {
        return false;
    }
    cfg_parse(cfg, buf, n);

    return true;
}
