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

#include "ini.h"

#include <agon/mos.h>
#include <string.h>

// A settings file is a handful of short lines; reading it whole costs less than
// buffering, and anything longer is not a settings file. Sized to leave room for
// the sections this version writes plus a good deal of the reader's own notes.
//
// It is not only the render buffer: cfg_update refuses to rewrite a file this
// long or longer, because it would be holding only the front of it. So the
// headroom above what cfg_render produces is the room a reader has to add notes
// before their colour changes quietly stop being saved. The font setting's
// commented example took the rendered file to around 950 bytes, which left far
// too little of it.
#define CFG_MAX 2048

// Where a setting's value lives in the program's struct. An int setting's is
// read with value_of, a text one's with text_of; the kind in the table says
// which applies, and the writers skip what the other kind would mean.
static int* int_of(void* values, const cfg_setting* st) {
    return (int*) ((char*) values + st->at);
}

// An int setting's value, or -1 -- which the writers read as "this version has
// nothing to say about that line" and copy it through untouched -- for a text
// setting.
static int value_of(const void* values, const cfg_setting* st) {
    if (st->kind != CFG_INT) {
        return -1;
    }

    return *(const int*) ((const char*) values + st->at);
}

// What a text setting should be written as, or NULL for "say nothing and leave
// the line alone". An empty string is a real answer -- it is how the file asks
// for nothing, a font of none -- so it is not the same as NULL.
static const char* text_of(const void* values, const cfg_setting* st) {
    if (st->kind != CFG_STR) {
        return NULL;
    }
    const char* text = (const char*) values + st->at;
    if (text[0] != 0) {
        return text;
    }
    if (st->none >= 0 && *(const bool*) ((const char*) values + st->none)) {
        return "";
    }

    return NULL;
}

// Whether `values` has anything to write for a setting.
static bool has_value(const void* values, const cfg_setting* st) {
    return st->kind == CFG_STR ? text_of(values, st) != NULL
                               : value_of(values, st) >= 0;
}

void cfg_defaults(const cfg_schema* sc, void* values) {
    for (int i = 0; i < sc->n; i++) {
        const cfg_setting* st = &sc->settings[i];
        if (st->kind == CFG_STR) {
            ((char*) values + st->at)[0] = 0;
            if (st->none >= 0) {
                *(bool*) ((char*) values + st->none) = false;
            }
            continue;
        }
        *int_of(values, st) = -1;
    }
}

// Which known setting a line names, given the section it appears in, or -1.
//
// A name appearing before any heading is matched on the name alone. The first
// settings file AED wrote had no headings at all, so this is what keeps one of
// those working instead of reading as an empty file; it also means a hand-edited
// file that is missing its heading still does what it plainly says. A name under
// the *wrong* heading is still ignored -- that is the scoping the sections are
// for, and it is what leaves room for a later [syntax] to have its own `fg`.
static int setting_index(const cfg_schema* sc, const char* section, int seclen,
                         const char* name, int namelen) {
    for (int i = 0; i < sc->n; i++) {
        if (!ini_name_is(name, namelen, sc->settings[i].name)) {
            continue;
        }
        if (seclen == 0 || ini_name_is(section, seclen, sc->settings[i].section)) {
            return i;
        }
    }

    return -1;
}

void cfg_parse(const cfg_schema* sc, void* values, const char* text, int len) {
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

        const line ln = ini_read_line(text, start, end);
        if (ln.kind == LINE_SECTION) {
            section = ln.name;
            seclen = ln.namelen;
            continue;
        }
        if (ln.kind != LINE_SETTING) {
            continue;
        }

        const int idx = setting_index(sc, section, seclen, ln.name, ln.namelen);
        if (idx < 0) {
            continue;   // unknown section or name: a later version's, perhaps
        }
        const cfg_setting* st = &sc->settings[idx];
        if (st->kind == CFG_STR) {
            // A value too long to hold is dropped rather than truncated: half a
            // path names a different file, and opening the wrong one silently
            // is worse than not opening one at all.
            char* dst = (char*) values + st->at;
            if (ln.valuelen > 0 && ln.valuelen < st->size) {
                memcpy(dst, ln.value, (size_t) ln.valuelen);
                dst[ln.valuelen] = 0;
            }
            continue;
        }
        int n = 0;
        if (ini_parse_number(ln.value, ln.valuelen, &n)) {
            *int_of(values, st) = n;
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

int cfg_put_text(char* out, int at, int max, const char* text) {
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

// A setting whose value is text. An empty value is written out
// as such -- `font =` with nothing after it -- which is how the file says "no
// font", since cfg_parse ignores a setting with no value.
static int put_str_setting(char* out, int at, int max, const char* name,
                           const char* value) {
    at = cfg_put_text(out, at, max, name);
    at = cfg_put_text(out, at, max, " = ");
    at = cfg_put_text(out, at, max, value);

    return cfg_put_text(out, at, max, "\r\n");
}

int cfg_put_setting(char* out, int at, int max, const char* name, int value) {
    at = cfg_put_text(out, at, max, name);
    at = cfg_put_text(out, at, max, " = ");
    at = put_number(out, at, max, value);

    return cfg_put_text(out, at, max, "\r\n");
}

int cfg_render(const cfg_schema* sc, const void* values, char* buf, int max) {
    const int at = sc->render(values, buf, max);

    return at < 0 ? 0 : at;
}

// Emits any settings belonging to `section` that the file did not already
// carry, so a setting saved for the first time is not silently dropped.
static int flush_section(const cfg_schema* sc, const void* values,
                         const char* section, int seclen,
                         const bool* done, char* out, int at, int max) {
    for (int k = 0; k < sc->n; k++) {
        if (done[k] || !ini_name_is(section, seclen, sc->settings[k].section)) {
            continue;
        }
        if (sc->settings[k].kind == CFG_STR) {
            const char* text = text_of(values, &sc->settings[k]);
            if (text != NULL) {
                at = put_str_setting(out, at, max, sc->settings[k].name, text);
            }
            continue;
        }
        if (value_of(values, &sc->settings[k]) < 0) {
            continue;
        }
        at = cfg_put_setting(out, at, max, sc->settings[k].name, value_of(values, &sc->settings[k]));
    }

    return at;
}

// Copies `in` to `out`, substituting new values for the settings `values` sets and
// leaving everything else byte for byte as it was.
static int merge(const cfg_schema* sc, const void* values,
                 const char* in, int inlen, char* out, int max) {
    bool done[CFG_SETTINGS_MAX];
    if (sc->n > CFG_SETTINGS_MAX) {
        return -1;          // a schema too big to keep track of: write nothing
    }
    for (int k = 0; k < sc->n; k++) {
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

        const line ln = ini_read_line(in, lstart, lend);

        if (ln.kind == LINE_SECTION) {
            // Leaving a section: anything it should have carried goes in before
            // the next heading, not at the end of the file under someone else's.
            at = flush_section(sc, values, section, seclen, done, out, at, max);
            for (int k = 0; k < sc->n; k++) {
                if (ini_name_is(section, seclen, sc->settings[k].section)) {
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
            idx = setting_index(sc, section, seclen, ln.name, ln.namelen);
        }

        // A string setting already in the file: replace what follows the '=',
        // keeping the name as written and any comment after it, exactly as the
        // numeric path does.
        if (idx >= 0 && sc->settings[idx].kind == CFG_STR) {
            const char* text = text_of(values, &sc->settings[idx]);
            done[idx] = true;
            if (text != NULL) {
                int tail = lend;
                if (tail > lstart && in[tail - 1] == '\r') {
                    tail--;
                }
                at = put_raw(out, at, max, in + lstart, (ln.eq + 1) - lstart);
                at = cfg_put_text(out, at, max, " ");
                at = cfg_put_text(out, at, max, text);
                if (ln.cut < lend) {
                    at = cfg_put_text(out, at, max, "  ");
                    at = put_raw(out, at, max, in + ln.cut, tail - ln.cut);
                }
                at = put_raw(out, at, max, in + tail, rawend - tail);
                if (at < 0) {
                    return -1;
                }
                continue;
            }
            at = put_raw(out, at, max, in + lstart, rawend - lstart);
            if (at < 0) {
                return -1;
            }
            continue;
        }

        int newval = idx >= 0 ? value_of(values, &sc->settings[idx]) : -1;

        // A value already equal to what we would write leaves the line alone,
        // so saving one setting never disturbs another's formatting.
        if (newval >= 0) {
            int current = 0;
            if (ini_parse_number(ln.value, ln.valuelen, &current) && current == newval) {
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
        at = cfg_put_text(out, at, max, " ");
        at = put_number(out, at, max, newval);
        if (ln.cut < lend) {
            at = cfg_put_text(out, at, max, "  ");
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
        // A lone CR here is a truncated CRLF, not text: the file's last line
        // lost its LF somewhere. Appending a whole ending after it would leave
        // \r\r\n. Drop it and write one proper ending instead.
        if (out[at - 1] == '\r') {
            at--;
        }
        at = cfg_put_text(out, at, max, "\r\n");
    }
    at = flush_section(sc, values, section, seclen, done, out, at, max);
    for (int k = 0; k < sc->n; k++) {
        if (ini_name_is(section, seclen, sc->settings[k].section)) {
            done[k] = true;
        }
    }
    for (int k = 0; k < sc->n; k++) {
        const bool has = has_value(values, &sc->settings[k]);
        if (done[k] || !has) {
            continue;
        }
        at = cfg_put_text(out, at, max, "\r\n[");
        at = cfg_put_text(out, at, max, sc->settings[k].section);
        at = cfg_put_text(out, at, max, "]\r\n");
        at = flush_section(sc, values, sc->settings[k].section,
                           (int) strlen(sc->settings[k].section), done, out, at, max);
        for (int j = 0; j < sc->n; j++) {
            if (ini_name_is(sc->settings[k].section, (int) strlen(sc->settings[k].section),
                        sc->settings[j].section)) {
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

bool cfg_save(const cfg_schema* sc, const void* values, const char* path) {
    if (path == NULL) {
        return false;
    }

    static char buf[CFG_MAX];
    const int n = cfg_render(sc, values, buf, CFG_MAX);
    if (n <= 0) {
        return false;
    }

    return write_file(path, buf, n);
}

bool cfg_update(const cfg_schema* sc, const void* values, const char* path) {
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
        return cfg_save(sc, values, path);   // nothing to merge into
    }
    if (inlen >= CFG_MAX) {
        // The file is at least as long as the buffer, so it may well be longer
        // and we are holding only the front of it. Writing that back would
        // truncate the rest away. Losing a colour choice is a far smaller cost
        // than eating the reader's file, so leave it exactly as it is.
        return false;
    }

    static char out[CFG_MAX];
    const int n = merge(sc, values, in, inlen, out, CFG_MAX);
    if (n <= 0) {
        return false;
    }

    return write_file(path, out, n);
}

bool cfg_migrate(const char* old, const char* path) {
    if (old == NULL) {
        return true;        // nothing was ever called anything else
    }

    // Already moved. An old file beside it is somebody else's file now.
    char have = mos_fopen(path, FA_READ);
    if (have != 0) {
        mos_fclose(have);

        return true;
    }

    char in = mos_fopen(old, FA_READ);
    if (in == 0) {
        return true;        // neither file: a first run, and nothing to move
    }

    mos_mkdir(CFG_DIR);
    char out = mos_fopen(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (out == 0) {
        mos_fclose(in);

        return false;       // the old file is still waiting to be moved
    }

    /*
     * Copied a bufferful at a time rather than read whole. Settings files are
     * small, but this one belongs to the reader and may have anything in it --
     * comments, sections AED does not know, a section for something else --
     * and none of that should be lost for being longer than a buffer.
     */
    static char buf[CFG_MAX];
    bool ok = true;
    for (;;) {
        const unsigned n = mos_fread(in, buf, CFG_MAX);
        if (n == 0) {
            break;
        }
        if (mos_fwrite(out, buf, n) != n) {
            ok = false;
            break;
        }
        if (n < CFG_MAX) {
            break;
        }
    }
    mos_fclose(in);
    mos_fclose(out);

    if (!ok) {
        // A half-written settings file would be read as the whole of the
        // reader's settings next time. Take it away and leave the old one
        // where it is, so the next run has something to try again from.
        mos_del(path);

        return false;
    }
    mos_del(old);

    return true;
}

bool cfg_load(const cfg_schema* sc, void* values, const char* path) {
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
    cfg_parse(sc, values, buf, n);

    return true;
}
