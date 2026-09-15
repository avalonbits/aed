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

#include "syntax.h"

#include "ini.h"

#include <agon/mos.h>
#include <string.h>

// The name a theme file uses for each class, in class order. A theme names the
// class rather than a scope, because a scope is a grammar's business and there
// are hundreds of them.
static const char* CLASS_NAME[TOK_N] = {
    "text", "comment", "string", "number", "keyword",
    "type", "preproc", "label", "operator",
};

const char* syn_class_name(tok_class c) {
    return (c >= 0 && c < TOK_N) ? CLASS_NAME[c] : CLASS_NAME[TOK_TEXT];
}

// A scope's first component decides its class, so `keyword.control.c` and
// `keyword.other` are both keywords without either being listed. The
// preprocessor is the one exception: it reads as a keyword by that rule, and
// wants its own colour badly enough to be worth the special case.
tok_class syn_class_of(const char* scope, int len) {
    if (scope == NULL || len <= 0) {
        return TOK_TEXT;
    }
    if (ini_name_is(scope, len, "keyword.control.preprocessor")) {
        return TOK_PREPROC;
    }
    int head = 0;
    while (head < len && scope[head] != '.') {
        head++;
    }
    static const struct { const char* head; tok_class cls; } HEADS[] = {
        { "comment",     TOK_COMMENT  },
        { "string",      TOK_STRING   },
        { "constant",    TOK_NUMBER   },
        { "keyword",     TOK_KEYWORD  },
        { "storage",     TOK_TYPE     },
        { "entity",      TOK_LABEL    },
        { "punctuation", TOK_OPERATOR },
    };
    for (int i = 0; i < (int) (sizeof(HEADS) / sizeof(HEADS[0])); i++) {
        if (ini_name_is(scope, head, HEADS[i].head)) {
            return HEADS[i].cls;
        }
    }

    return TOK_TEXT;
}

void theme_clear(theme* t) {
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->fg = -1;
    t->bg = -1;
    for (int i = 0; i < TOK_N; i++) {
        t->colour[i] = -1;      // says nothing; theme_colour falls back
    }
}

char theme_colour(const theme* t, tok_class c) {
    if (t == NULL || c < 0 || c >= TOK_N || t->colour[c] < 0) {
        return (t != NULL && t->colour[TOK_TEXT] >= 0) ? t->colour[TOK_TEXT] : -1;
    }

    return t->colour[c];
}

bool theme_covers(const theme* t, int bg) {
    if (t == NULL || !t->loaded) {
        return false;
    }
    for (int i = 0; i < t->ncovers; i++) {
        if (t->covers[i] == (char) bg) {
            return true;
        }
    }

    return false;
}

// `covers = 0 1 4 5`: the backgrounds the theme's author meant it for. Parsed
// here rather than through ini_parse_number, which reads one number and a
// whole value.
static void parse_covers(theme* t, const char* v, int len) {
    t->ncovers = 0;
    int at = 0;
    while (at < len && t->ncovers < THEME_MAX_COVERS) {
        while (at < len && (v[at] == ' ' || v[at] == '\t' || v[at] == ',')) {
            at++;
        }
        const int start = at;
        while (at < len && v[at] >= '0' && v[at] <= '9') {
            at++;
        }
        if (at == start) {
            break;              // not a number; the rest is not either
        }
        int n = 0;
        if (ini_parse_number(v + start, at - start, &n) && n >= 0 && n < 64) {
            t->covers[(int) t->ncovers++] = (char) n;
        }
    }
}

bool theme_load(theme* t, const char* path) {
    if (t == NULL || path == NULL) {
        return false;
    }
    static char buf[1024];
    const char fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        return false;
    }
    const unsigned got = mos_fread(fh, buf, (unsigned) sizeof(buf) - 1);
    mos_fclose(fh);
    if (got == 0) {
        return false;
    }
    const int len = (int) got;

    // Into a scratch theme, so a file that turns out to be unreadable leaves
    // the caller with the theme it already had rather than half of a new one.
    static theme t2;
    theme_clear(&t2);

    int at = 0;
    bool in_theme = false;
    bool in_colours = false;
    while (at < len) {
        int end = at;
        while (end < len && buf[end] != '\n') {
            end++;
        }
        const line ln = ini_read_line(buf, at, end);
        if (ln.kind == LINE_SECTION) {
            in_theme = ini_name_is(ln.name, ln.namelen, "theme");
            in_colours = ini_name_is(ln.name, ln.namelen, "colours")
                      || ini_name_is(ln.name, ln.namelen, "colors");
        } else if (ln.kind == LINE_SETTING && in_theme) {
            int n = 0;
            if (ini_name_is(ln.name, ln.namelen, "name")) {
                int k = ln.valuelen < THEME_NAME_MAX - 1
                      ? ln.valuelen : THEME_NAME_MAX - 1;
                memcpy(t2.name, ln.value, (size_t) k);
                t2.name[k] = 0;
            } else if (ini_name_is(ln.name, ln.namelen, "covers")) {
                parse_covers(&t2, ln.value, ln.valuelen);
            } else if (ini_name_is(ln.name, ln.namelen, "fg")
                       && ini_parse_number(ln.value, ln.valuelen, &n)) {
                t2.fg = (char) n;
            } else if (ini_name_is(ln.name, ln.namelen, "bg")
                       && ini_parse_number(ln.value, ln.valuelen, &n)) {
                t2.bg = (char) n;
            }
        } else if (ln.kind == LINE_SETTING && in_colours) {
            int n = 0;
            if (!ini_parse_number(ln.value, ln.valuelen, &n)) {
                at = end + 1;
                continue;       // a colour that is not a number is no colour
            }
            for (int c = 0; c < TOK_N; c++) {
                if (ini_name_is(ln.name, ln.namelen, CLASS_NAME[c])) {
                    t2.colour[c] = (char) n;
                    break;
                }
            }
        }
        at = end + 1;
    }

    t2.loaded = true;
    *t = t2;

    return true;
}
