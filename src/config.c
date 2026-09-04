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
// buffering, and anything longer is not a settings file. Sized to leave room for
// the sections this version writes plus a good deal of the reader's own notes.
#define CFG_MAX 1024

// The settings this version knows, each identified by its INI section and name.
// Adding one is a line here plus a case in cfg_field: parsing, writing and
// updating all read from this table.
typedef struct _setting_id {
    const char* section;
    const char* name;
} setting_id;

static const setting_id SETTINGS[] = {
    { "editor",  "tab" },
    { "colours", "fg"  },
    { "colours", "bg"  },
    { "vdp",     "ctrl_pause_frames" },
};
#define N_SETTINGS ((int)(sizeof(SETTINGS) / sizeof(SETTINGS[0])))

static int* cfg_field(config* cfg, int i) {
    switch (i) {
        case 0:  return &cfg->tab_size;
        case 1:  return &cfg->fg;
        case 2:  return &cfg->bg;
        case 3:  return &cfg->ctrl_pause;
        default: return NULL;
    }
}

static int cfg_value(const config* cfg, int i) {
    switch (i) {
        case 0:  return cfg->tab_size;
        case 1:  return cfg->fg;
        case 2:  return cfg->bg;
        case 3:  return cfg->ctrl_pause;
        default: return -1;
    }
}

void cfg_defaults(config* cfg) {
    cfg->tab_size = -1;
    cfg->fg = -1;
    cfg->bg = -1;
    cfg->ctrl_pause = -1;
}

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
static bool name_is(const char* s, int len, const char* name) {
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

// One line, dissected. `kind` says which of the others mean anything.
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

static line read_line(const char* s, int start, int end) {
    line ln;
    ln.kind = LINE_OTHER;
    ln.eq = -1;
    ln.name = NULL;
    ln.namelen = 0;
    ln.value = NULL;
    ln.valuelen = 0;

    int cut = end;
    for (int c = start; c < end; c++) {
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

// Which known setting a line names, given the section it appears in, or -1.
//
// A name appearing before any heading is matched on the name alone. The first
// settings file AED wrote had no headings at all, so this is what keeps one of
// those working instead of reading as an empty file; it also means a hand-edited
// file that is missing its heading still does what it plainly says. A name under
// the *wrong* heading is still ignored -- that is the scoping the sections are
// for, and it is what leaves room for a later [syntax] to have its own `fg`.
static int setting_index(const char* section, int seclen,
                         const char* name, int namelen) {
    for (int i = 0; i < N_SETTINGS; i++) {
        if (!name_is(name, namelen, SETTINGS[i].name)) {
            continue;
        }
        if (seclen == 0 || name_is(section, seclen, SETTINGS[i].section)) {
            return i;
        }
    }

    return -1;
}

void cfg_parse(config* cfg, const char* text, int len) {
    if (text == NULL || len <= 0) {
        return;
    }

    const char* section = "";
    int seclen = 0;
    int i = 0;

    while (i < len) {
        const int start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        const int end = i;
        i++;

        const line ln = read_line(text, start, end);
        if (ln.kind == LINE_SECTION) {
            section = ln.name;
            seclen = ln.namelen;
            continue;
        }
        if (ln.kind != LINE_SETTING) {
            continue;
        }

        const int idx = setting_index(section, seclen, ln.name, ln.namelen);
        if (idx < 0) {
            continue;   // unknown section or name: a later version's, perhaps
        }
        int n = 0;
        if (parse_number(ln.value, ln.valuelen, &n)) {
            *cfg_field(cfg, idx) = n;
        }
    }
}

static int put_raw(char* out, int at, int max, const char* src, int n) {
    if (at < 0 || n < 0 || at + n > max) {
        return -1;
    }
    memcpy(out + at, src, (size_t) n);

    return at + n;
}

static int put_text(char* out, int at, int max, const char* text) {
    return put_raw(out, at, max, text, (int) strlen(text));
}

static int put_number(char* out, int at, int max, int value) {
    char digits[8];
    int dn = 0;
    if (value == 0) {
        digits[dn++] = '0';
    }
    for (int v = value; v > 0; v /= 10) {
        digits[dn++] = (char)('0' + (v % 10));
    }
    if (at < 0 || at + dn > max) {
        return -1;
    }
    while (dn > 0) {
        out[at++] = digits[--dn];
    }

    return at;
}

// "name = value\r\n". CRLF because that is what the editor writes into text
// files, and this one is meant to be opened in it.
static int put_setting(char* out, int at, int max, const char* name, int value) {
    at = put_text(out, at, max, name);
    at = put_text(out, at, max, " = ");
    at = put_number(out, at, max, value);

    return put_text(out, at, max, "\r\n");
}

static const char* HEADER =
    "# AED settings.\r\n"
    "#\r\n"
    "# An INI file: [section] headings, then name = value lines. Blank lines\r\n"
    "# are ignored and '#' or ';' starts a comment. Sections and settings AED\r\n"
    "# does not recognise are skipped, so this file stays readable by older and\r\n"
    "# newer versions alike. Edit and restart AED to apply.\r\n";

int cfg_render(const config* cfg, char* buf, int max) {
    int at = put_text(buf, 0, max, HEADER);
    at = put_text(buf, at, max,
        "\r\n[editor]\r\n"
        "# How wide a tab renders, in columns. 1 to 16.\r\n");
    at = put_setting(buf, at, max, "tab", cfg->tab_size);
    at = put_text(buf, at, max,
        "\r\n[colours]\r\n"
        "# Text and background colour, as Agon colour numbers. These were\r\n"
        "# taken from the colours your Agon was already using.\r\n");
    at = put_setting(buf, at, max, "fg", cfg->fg);
    at = put_setting(buf, at, max, "bg", cfg->bg);

    return at < 0 ? 0 : at;
}

// Emits any settings belonging to `section` that the file did not already
// carry, so a setting saved for the first time is not silently dropped.
static int flush_section(const config* cfg, const char* section, int seclen,
                         const bool* done, char* out, int at, int max) {
    for (int k = 0; k < N_SETTINGS; k++) {
        if (done[k] || cfg_value(cfg, k) < 0) {
            continue;
        }
        if (!name_is(section, seclen, SETTINGS[k].section)) {
            continue;
        }
        at = put_setting(out, at, max, SETTINGS[k].name, cfg_value(cfg, k));
    }

    return at;
}

// Copies `in` to `out`, substituting new values for the settings `cfg` sets and
// leaving everything else byte for byte as it was.
static int merge(const config* cfg, const char* in, int inlen, char* out, int max) {
    bool done[N_SETTINGS];
    for (int k = 0; k < N_SETTINGS; k++) {
        done[k] = false;
    }

    const char* section = "";
    int seclen = 0;
    int at = 0;
    int i = 0;

    while (i < inlen) {
        const int lstart = i;
        while (i < inlen && in[i] != '\n') {
            i++;
        }
        const int lend = i;
        if (i < inlen) {
            i++;
        }
        const int rawend = i;

        const line ln = read_line(in, lstart, lend);

        if (ln.kind == LINE_SECTION) {
            // Leaving a section: anything it should have carried goes in before
            // the next heading, not at the end of the file under someone else's.
            at = flush_section(cfg, section, seclen, done, out, at, max);
            for (int k = 0; k < N_SETTINGS; k++) {
                if (name_is(section, seclen, SETTINGS[k].section)) {
                    done[k] = true;
                }
            }
            section = ln.name;
            seclen = ln.namelen;
            at = put_raw(out, at, max, in + lstart, rawend - lstart);
            if (at < 0) {
                return -1;
            }
            continue;
        }

        int idx = -1;
        if (ln.kind == LINE_SETTING) {
            idx = setting_index(section, seclen, ln.name, ln.namelen);
        }
        int newval = idx >= 0 ? cfg_value(cfg, idx) : -1;

        // A value already equal to what we would write leaves the line alone,
        // so saving one setting never disturbs another's formatting.
        if (newval >= 0) {
            int current = 0;
            if (parse_number(ln.value, ln.valuelen, &current) && current == newval) {
                newval = -1;
            }
            done[idx] = true;
        }

        if (newval < 0) {
            at = put_raw(out, at, max, in + lstart, rawend - lstart);
            if (at < 0) {
                return -1;
            }
            continue;
        }

        // Where the line's text stops and its ending begins. lend is the LF, so
        // on a CRLF file the CR sits one before it -- inside the text that is
        // about to be replaced. Copying only from lend leaves the CR behind and
        // quietly turns that one line into a bare LF, which is how a file that
        // was all CRLF ends up mixed after a colour change.
        int tail = lend;
        if (tail > lstart && in[tail - 1] == '\r') {
            tail--;
        }

        // Keep the name exactly as written, replace the value, keep any comment.
        at = put_raw(out, at, max, in + lstart, (ln.eq + 1) - lstart);
        at = put_text(out, at, max, " ");
        at = put_number(out, at, max, newval);
        if (ln.cut < lend) {
            at = put_text(out, at, max, "  ");
            at = put_raw(out, at, max, in + ln.cut, tail - ln.cut);
        }
        at = put_raw(out, at, max, in + tail, rawend - tail);
        if (at < 0) {
            return -1;
        }
    }

    // The last section in the file, then any section the file never had at all.
    // A file whose last line has no newline needs one before anything is added.
    if (at > 0 && out[at - 1] != '\n') {
        at = put_text(out, at, max, "\r\n");
    }
    at = flush_section(cfg, section, seclen, done, out, at, max);
    for (int k = 0; k < N_SETTINGS; k++) {
        if (name_is(section, seclen, SETTINGS[k].section)) {
            done[k] = true;
        }
    }
    for (int k = 0; k < N_SETTINGS; k++) {
        if (done[k] || cfg_value(cfg, k) < 0) {
            continue;
        }
        at = put_text(out, at, max, "\r\n[");
        at = put_text(out, at, max, SETTINGS[k].section);
        at = put_text(out, at, max, "]\r\n");
        at = flush_section(cfg, SETTINGS[k].section,
                           (int) strlen(SETTINGS[k].section), done, out, at, max);
        for (int j = 0; j < N_SETTINGS; j++) {
            if (name_is(SETTINGS[k].section, (int) strlen(SETTINGS[k].section),
                        SETTINGS[j].section)) {
                done[j] = true;
            }
        }
        if (at < 0) {
            return -1;
        }
    }

    return at;
}

static bool write_file(const char* path, const char* buf, int n) {
    // The directory is very likely missing on first run. mos_mkdir failing
    // because it already exists is indistinguishable from any other failure
    // here, so just try the open and let that decide.
    mos_mkdir(CFG_DIR);

    char fh = mos_fopen(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return false;
    }
    const unsigned written = mos_fwrite(fh, (char*) buf, (unsigned) n);
    mos_fclose(fh);

    if (written != (unsigned) n) {
        // A full card or a bad write leaves a truncated file. That is worse
        // than no file at all: it still parses, so the next startup would load
        // it and never rewrite the missing settings. FA_CREATE_ALWAYS has
        // already discarded whatever was there before, so there is nothing to
        // preserve by keeping the remnant.
        mos_del(path);

        return false;
    }

    return true;
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

    return write_file(path, buf, n);
}

bool cfg_update(const config* cfg, const char* path) {
    if (path == NULL) {
        return false;
    }

    static char in[CFG_MAX];
    int inlen = 0;

    char rh = mos_fopen(path, FA_READ);
    if (rh != 0) {
        inlen = (int) mos_fread(rh, in, CFG_MAX);
        mos_fclose(rh);
    }
    if (inlen <= 0) {
        return cfg_save(cfg, path);   // nothing to merge into
    }
    if (inlen >= CFG_MAX) {
        // The file is at least as long as the buffer, so it may well be longer
        // and we are holding only the front of it. Writing that back would
        // truncate the rest away. Losing a colour choice is a far smaller cost
        // than eating the reader's file, so leave it exactly as it is.
        return false;
    }

    static char out[CFG_MAX];
    const int n = merge(cfg, in, inlen, out, CFG_MAX);
    if (n <= 0) {
        return false;
    }

    return write_file(path, out, n);
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
