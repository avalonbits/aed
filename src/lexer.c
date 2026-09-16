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

// A word is a run of these. The dot is in because assembly wants it at both
// ends of a name -- `.db` is a directive and `rst.lil` is one opcode -- and
// nothing else in the three languages is hurt by it.
static bool is_word(const syntax* g, char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '.') {
        return true;
    }
    if (g != NULL) {
        for (int i = 0; i < SYN_WORDCHARS_MAX && g->wordchars[i] != 0; i++) {
            if (g->wordchars[i] == c) {
                return true;
            }
        }
    }

    return false;
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static char fold(char c) {
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static int cmp_word(const char* a, int alen, const char* b, bool nocase) {
    for (int i = 0; i < alen; i++) {
        const char x = nocase ? fold(a[i]) : a[i];
        const char y = nocase ? fold(b[i]) : b[i];
        if (b[i] == 0 || x != y) {
            return (b[i] == 0 || x > y) ? 1 : -1;
        }
    }

    return b[alen] == 0 ? 0 : -1;
}

/*
 * Is the word at `s` one of this rule's?
 *
 * The rule's offsets are sorted at load, so this is a binary search: six
 * comparisons against sixty opcodes rather than sixty. That is not a
 * micro-optimisation -- a line holds a dozen words and a repaint sixty lines,
 * so a linear scan would be tens of thousands of character comparisons for one
 * screen, which is more than the painting itself costs.
 */
static bool in_words(const syntax* g, const syn_rule* r, const char* s, int n) {
    int lo = 0;
    int hi = r->word_n - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const char* w = g->words + g->wordoff[r->word_at + mid];
        const int c = cmp_word(s, n, w, g->nocase);
        if (c == 0) {
            return true;
        }
        if (c < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }

    return false;
}

static bool lit_at(const char* s, int len, int at, const char* lit, int n,
                   bool nocase) {
    if (n == 0 || at + n > len) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        const char x = nocase ? fold(s[at + i]) : s[at + i];
        const char y = nocase ? fold(lit[i]) : lit[i];
        if (x != y) {
            return false;
        }
    }

    return true;
}

/*
 * Where a span that has already opened finishes, starting the search at `i`.
 *
 * Returns one past the closing literal, or -1 when the line runs out first --
 * which is the line ending inside the span, and the only way a state crosses a
 * line break.
 */
static int span_end(const syn_rule* r, const char* s, int len, int i,
                    bool nocase) {
    while (i < len) {
        if (r->escape != 0 && s[i] == r->escape && i + 1 < len) {
            i += 2;
            continue;
        }
        if (lit_at(s, len, i, r->close, r->nclose, nocase)) {
            return i + r->nclose;
        }
        i++;
    }

    return -1;
}

/*
 * Whether a literal made of word characters is sitting on word boundaries.
 *
 * BASIC is why this exists: `REM` begins a comment, and without this `REMOVE`
 * would begin one too, commenting out the rest of the line. A literal that is
 * punctuation -- `//`, `;`, `#` -- has no boundary to respect and is
 * unaffected, so C and assembly read exactly as they did.
 */
static bool lit_bounded(const syntax* g, const char* s, int len, int at,
                        const char* lit, int n) {
    if (n <= 0) {
        return true;
    }
    if (is_word(g, lit[0]) && at > 0 && is_word(g, s[at - 1])) {
        return false;
    }
    if (is_word(g, lit[n - 1]) && at + n < len && is_word(g, s[at + n])) {
        return false;
    }

    return true;
}

/*
 * How many bytes of a number start at `at`, or zero.
 *
 * Built in rather than expressible as a rule, because every language wants one
 * and none of them write it the same way. What is accepted is the union that
 * costs nothing to accept: a leading digit, or one of assembly's sigils, and
 * then the characters numbers are made of. `1st` is not a number and neither is
 * a bare `$`.
 */
static int number_at(const syntax* g, const char* s, int len, int at) {
    int i = at;
    // `$FF` and `%1010` in assembly, `&FF` in BASIC, `#10` where a language
    // writes it that way.
    if (i < len && (s[i] == '$' || s[i] == '%' || s[i] == '#' || s[i] == '&')) {
        i++;
        const int start = i;
        while (i < len && (is_digit(s[i])
                           || (fold(s[i]) >= 'a' && fold(s[i]) <= 'f'))) {
            i++;
        }
        if (i == start) {
            return 0;
        }
        /*
         * The run has to end where a word would. Without this, C's `&foo`
         * reads as the number `&f` followed by `oo`, because `f` is a hex
         * digit -- which is the whole reason `&` could not simply be added to
         * the list above.
         */
        if (i < len && is_word(g, s[i])) {
            return 0;
        }

        return i - at;
    }
    if (i >= len || !is_digit(s[i])) {
        return 0;
    }
    bool dot = false;
    while (i < len) {
        if (is_digit(s[i]) || fold(s[i]) == 'x'
                || (fold(s[i]) >= 'a' && fold(s[i]) <= 'f')) {
            i++;
            continue;
        }
        // One decimal point, and only with a digit behind it, so `10.5` is a
        // number while `1.0.2` and assembly's `rst.lil` are not.
        if (s[i] == '.' && !dot && i + 1 < len && is_digit(s[i + 1])) {
            dot = true;
            i += 2;
            continue;
        }
        break;
    }
    // A number runs up to a word character it did not consume, which is what
    // keeps `1st` out: the `s` and `t` are word characters, so this is a word.
    if (i < len && is_word(g, s[i])) {
        return 0;
    }

    return i - at;
}

// Adds a run, merging with the one before when the class has not changed --
// which is what keeps the painting to one colour change a run.
static int add_run(tok_run* out, int n, int max, int end, tok_class cls) {
    if (n > 0 && out[n - 1].cls == (char) cls) {
        out[n - 1].end = end;

        return n;
    }
    if (n >= max) {
        if (n > 0) {
            out[n - 1].end = end;   // no room: the last run swallows the rest
        }

        return n;
    }
    out[n].end = end;
    out[n].cls = (char) cls;

    return n + 1;
}


void syn_clear(syntax* g) {
    if (g != NULL) {
        memset(g, 0, sizeof(*g));
    }
}

// ".c .h .cc" against "main.c". The extension is whatever follows the last dot,
// and a name without one matches nothing.
bool syn_covers(const syntax* g, const char* fname) {
    if (g == NULL || !g->loaded || fname == NULL) {
        return false;
    }
    const char* dot = NULL;
    for (const char* p = fname; *p != 0; p++) {
        if (*p == '.') {
            dot = p;
        }
        if (*p == '/' || *p == '\\') {
            dot = NULL;         // a dot in a directory is not an extension
        }
    }
    if (dot == NULL) {
        return false;
    }
    int elen = 0;
    while (dot[elen] != 0) {
        elen++;
    }
    int at = 0;
    while (at < SYN_EXTS_MAX && g->exts[at] != 0) {
        while (at < SYN_EXTS_MAX && (g->exts[at] == ' ' || g->exts[at] == ',')) {
            at++;
        }
        const int start = at;
        while (at < SYN_EXTS_MAX && g->exts[at] != 0 && g->exts[at] != ' '
               && g->exts[at] != ',') {
            at++;
        }
        if (at > start && at - start == elen) {
            bool same = true;
            for (int i = 0; i < elen && same; i++) {
                same = fold(g->exts[start + i]) == fold(dot[i]);
            }
            if (same) {
                return true;
            }
        }
        if (at == start) {
            break;
        }
    }

    return false;
}

// The rest of a value after the verb, trimmed. `eol //` leaves `//`.
static int after_verb(const char* v, int len, int at) {
    while (at < len && (v[at] == ' ' || v[at] == '\t')) {
        at++;
    }

    return at;
}

// A literal, quoted or bare. Quoting is what lets a grammar name the character
// that ends a line's worth of comment in *its* language, which for assembly is
// the same ';' this file's own comments start with -- so it is written ';' and
// the quotes come off here.
static int copy_lit(char* dst, const char* v, int at, int len) {
    int n = 0;
    if (at < len && (v[at] == '"' || v[at] == '\'')) {
        const char q = v[at];
        at++;
        while (at + n < len && n < SYN_LIT_MAX && v[at + n] != q) {
            dst[n] = v[at + n];
            n++;
        }

        return n;
    }
    while (at + n < len && n < SYN_LIT_MAX && v[at + n] != ' '
           && v[at + n] != '\t') {
        dst[n] = v[at + n];
        n++;
    }

    return n;
}

// How far a literal reached in the value, so the next one can be found after
// it. Two more than its length when it was quoted.
static int lit_span(const char* v, int at, int len, int n) {
    return (at < len && (v[at] == '"' || v[at] == '\'')) ? n + 2 : n;
}

// Words are packed and their offsets sorted, so in_words can bisect. Insertion
// sort: a set is a few dozen and this runs once, when the grammar is read.
static void add_words(syntax* g, syn_rule* r, const char* v, int at, int len) {
    r->word_at = g->noffs;
    r->word_n = 0;
    while (at < len) {
        while (at < len && (v[at] == ' ' || v[at] == '\t' || v[at] == ',')) {
            at++;
        }
        const int start = at;
        while (at < len && v[at] != ' ' && v[at] != '\t' && v[at] != ',') {
            at++;
        }
        const int wlen = at - start;
        if (wlen == 0) {
            break;
        }
        if (g->nwords + wlen + 1 > SYN_WORDS_MAX
                || g->noffs >= SYN_WORDOFF_MAX) {
            break;              // full: the rest of the set is dropped
        }
        const int off = g->nwords;
        memcpy(g->words + off, v + start, (size_t) wlen);
        g->words[off + wlen] = 0;
        g->nwords += wlen + 1;

        // In place, among this rule's offsets only.
        int i = r->word_n;
        while (i > 0) {
            const char* prev = g->words + g->wordoff[r->word_at + i - 1];
            if (cmp_word(g->words + off, wlen, prev, g->nocase) >= 0) {
                break;
            }
            g->wordoff[r->word_at + i] = g->wordoff[r->word_at + i - 1];
            i--;
        }
        g->wordoff[r->word_at + i] = off;
        r->word_n++;
        g->noffs++;
    }
}

static const struct { const char* name; match_kind kind; } VERBS[] = {
    { "eol",    M_EOL    },
    { "span",   M_SPAN   },
    { "words",  M_WORDS  },
    { "bol",    M_BOL    },
    { "number", M_NUMBER },
    { "label",  M_LABEL  },
};

bool syn_load(syntax* g, const char* path) {
    if (g == NULL || path == NULL) {
        return false;
    }
    /*
     * The whole file, read before a line of it is parsed. So this bounds the
     * *file* rather than the grammar: every byte in it counts, comments and
     * blank lines included, while what the rules themselves may hold is
     * bounded separately by SYN_MAX_RULES, SYN_WORDS_MAX and SYN_WORDOFF_MAX.
     *
     * The two are easy to confuse and were. The grammar that ships for BASIC
     * uses 631 of its 1,024 bytes of keyword text, and its file sat 7 bytes
     * under a 2 KB buffer -- so a comment added to it overflowed the buffer
     * while the grammar was barely half full, the rules past the cut were
     * dropped, and the language half worked.
     */
    static char buf[4096];
    const char fh = mos_fopen(path, FA_READ);
    if (fh == 0) {
        return false;
    }
    const unsigned got = mos_fread(fh, buf, (unsigned) sizeof(buf) - 1);
    mos_fclose(fh);
    if (got == 0) {
        return false;
    }
    if (got >= (unsigned) sizeof(buf) - 1) {
        /*
         * The file is at least as long as there is room for, so there may be
         * more of it and this is only the front. Half a grammar is worse than
         * none: it colours some of a language and not the rest, which reads as
         * a bug in the grammar rather than a file that did not fit. Refused,
         * so the answer is "no highlighting" and the cause is findable.
         *
         * A file that cannot fit is a file, not a grammar that is too big.
         * Parsing it a chunk at a time would cost documentation nothing and
         * leave only the rules bounded, and is a change of its own.
         */
        return false;
    }
    const int len = (int) got;

    static syntax g2;
    syn_clear(&g2);

    // Two passes. `case` decides how words are sorted and compared, and it can
    // be written after them, so it is read before any rule is.
    for (int pass = 0; pass < 2; pass++) {
        int at = 0;
        bool in_syntax = false;
        bool in_match = false;
        while (at < len) {
            int end = at;
            while (end < len && buf[end] != '\n') {
                end++;
            }
            const line ln = ini_read_line(buf, at, end);
            if (ln.kind == LINE_SECTION) {
                in_syntax = ini_name_is(ln.name, ln.namelen, "syntax");
                in_match = ini_name_is(ln.name, ln.namelen, "match");
            } else if (ln.kind == LINE_SETTING && in_syntax && pass == 0) {
                if (ini_name_is(ln.name, ln.namelen, "name")) {
                    const int k = ln.valuelen < SYN_NAME_MAX - 1
                                ? ln.valuelen : SYN_NAME_MAX - 1;
                    memcpy(g2.name, ln.value, (size_t) k);
                } else if (ini_name_is(ln.name, ln.namelen, "extensions")) {
                    const int k = ln.valuelen < SYN_EXTS_MAX - 1
                                ? ln.valuelen : SYN_EXTS_MAX - 1;
                    memcpy(g2.exts, ln.value, (size_t) k);
                } else if (ini_name_is(ln.name, ln.namelen, "case")) {
                    g2.nocase = ini_name_is(ln.value, ln.valuelen,
                                            "insensitive");
                } else if (ini_name_is(ln.name, ln.namelen, "wordchars")) {
                    // Written run together -- `wordchars = $%` -- because
                    // separating them would need an escape for the space.
                    int w = 0;
                    for (int c = 0; c < ln.valuelen
                                    && w < SYN_WORDCHARS_MAX - 1; c++) {
                        if (ln.value[c] != ' ' && ln.value[c] != '\t') {
                            g2.wordchars[w++] = ln.value[c];
                        }
                    }
                }
            } else if (ln.kind == LINE_SETTING && in_match && pass == 1) {
                if (g2.nrules >= SYN_MAX_RULES) {
                    at = end + 1;
                    continue;
                }
                syn_rule* r = &g2.rules[g2.nrules];
                memset(r, 0, sizeof(*r));
                r->cls = (char) syn_class_of(ln.name, ln.namelen);

                int vat = 0;
                while (vat < ln.valuelen && ln.value[vat] != ' '
                       && ln.value[vat] != '\t') {
                    vat++;
                }
                int found = -1;
                for (int k = 0; k < 6; k++) {
                    if (ini_name_is(ln.value, vat, VERBS[k].name)) {
                        found = k;
                        break;
                    }
                }
                if (found < 0) {
                    at = end + 1;
                    continue;   // a verb nobody knows is a rule nobody applies
                }
                r->kind = (char) VERBS[found].kind;
                vat = after_verb(ln.value, ln.valuelen, vat);

                if (r->kind == M_WORDS) {
                    add_words(&g2, r, ln.value, vat, ln.valuelen);
                    if (r->word_n == 0) {
                        at = end + 1;
                        continue;
                    }
                } else if (r->kind == M_EOL || r->kind == M_BOL
                           || r->kind == M_SPAN) {
                    r->nopen = (char) copy_lit(r->open, ln.value, vat,
                                               ln.valuelen);
                    if (r->nopen == 0) {
                        at = end + 1;
                        continue;
                    }
                    if (r->kind == M_SPAN) {
                        vat = after_verb(ln.value, ln.valuelen,
                                         vat + lit_span(ln.value, vat,
                                                        ln.valuelen, r->nopen));
                        r->nclose = (char) copy_lit(r->close, ln.value, vat,
                                                    ln.valuelen);
                        if (r->nclose == 0) {
                            at = end + 1;
                            continue;
                        }
                        vat = after_verb(ln.value, ln.valuelen,
                                         vat + lit_span(ln.value, vat,
                                                        ln.valuelen,
                                                        r->nclose));
                        /*
                         * `escape <c>` and `multiline` may follow in either
                         * order, and either may be left out. Reading them in a
                         * loop rather than in sequence is what makes the order
                         * free, and an unknown word ends the rule rather than
                         * failing the file -- a grammar written against a
                         * later AED still loads on this one.
                         */
                        while (vat < ln.valuelen) {
                            if (ini_name_is(ln.value + vat, 6, "escape")) {
                                vat = after_verb(ln.value, ln.valuelen,
                                                 vat + 6);
                                if (vat < ln.valuelen) {
                                    r->escape = ln.value[vat];
                                    vat = after_verb(ln.value, ln.valuelen,
                                                     vat + 1);
                                }
                                continue;
                            }
                            if (ini_name_is(ln.value + vat, 9, "multiline")) {
                                r->multiline = 1;
                                vat = after_verb(ln.value, ln.valuelen,
                                                 vat + 9);
                                continue;
                            }
                            break;
                        }
                    }
                }
                g2.nrules++;
            }
            at = end + 1;
        }
    }

    if (g2.nrules == 0) {
        return false;           // a grammar that matches nothing is not one
    }
    g2.loaded = true;
    *g = g2;

    return true;
}

int syn_lex(const syntax* g, const char* line, int len, int in,
            int* out_state, tok_run* out, int max) {
    if (out == NULL) {
        max = 0;                    // state only; add_run writes nothing
    }
    if (g == NULL || !g->loaded || line == NULL || max < 0) {
        if (out_state != NULL) {
            *out_state = SYN_STATE_NONE;
        }

        return 0;
    }
    int n = 0;
    int at = 0;
    bool at_line_start = true;      // nothing but blanks seen yet
    int state = SYN_STATE_NONE;

    /*
     * A line that begins inside a span finishes that span before anything else
     * is looked at. The rest of the rules never see those columns, which is
     * what makes a quote inside a block comment ordinary text.
     *
     * A state naming a rule this grammar does not have is dropped rather than
     * trusted: grammars are reloaded when the file changes, and a state held
     * across that would otherwise colour by an index that has moved.
     */
    if (in != SYN_STATE_NONE) {
        const int k = in - 1;
        if (k >= 0 && k < g->nrules && g->rules[k].kind == M_SPAN
                && g->rules[k].multiline) {
            const syn_rule* r = &g->rules[k];
            const int e = span_end(r, line, len, 0, g->nocase);
            at = (e < 0 ? len : e);
            state = (e < 0 ? in : SYN_STATE_NONE);
            if (at > 0) {
                at_line_start = false;
                n = add_run(out, n, max, at, (tok_class) r->cls);
            }
        }
    }

    while (at < len) {
        int took = 0;
        tok_class cls = TOK_TEXT;

        for (int k = 0; k < g->nrules && took == 0; k++) {
            const syn_rule* r = &g->rules[k];
            switch ((match_kind) r->kind) {
                case M_EOL:
                    if (lit_at(line, len, at, r->open, r->nopen, g->nocase)
                            && lit_bounded(g, line, len, at, r->open,
                                           r->nopen)) {
                        took = len - at;
                        cls = (tok_class) r->cls;
                    }
                    break;
                case M_SPAN: {
                    if (!lit_at(line, len, at, r->open, r->nopen, g->nocase)) {
                        break;
                    }
                    const int e = span_end(r, line, len, at + r->nopen,
                                           g->nocase);
                    if (e < 0) {
                        // Unclosed. A multiline span hands the rest to the
                        // line below; anything else ends where the line does,
                        // so one stray quote colours one line rather than a
                        // file.
                        took = len - at;
                        if (r->multiline) {
                            state = k + 1;
                        }
                    } else {
                        took = e - at;
                    }
                    cls = (tok_class) r->cls;
                } break;
                case M_WORDS: {
                    if (!is_word(g, line[at])
                            || (at > 0 && is_word(g, line[at - 1]))) {
                        break;      // mid-word: not a word boundary
                    }
                    int i = at;
                    while (i < len && is_word(g, line[i])) {
                        i++;
                    }
                    if (in_words(g, r, line + at, i - at)) {
                        took = i - at;
                        cls = (tok_class) r->cls;
                    }
                } break;
                case M_BOL:
                    if (at_line_start
                            && lit_at(line, len, at, r->open, r->nopen,
                                      g->nocase)
                            && lit_bounded(g, line, len, at, r->open,
                                           r->nopen)) {
                        took = len - at;
                        cls = (tok_class) r->cls;
                    }
                    break;
                case M_LABEL: {
                    if (at != 0 || !is_word(g, line[at]) || is_digit(line[at])) {
                        break;      // only where a line starts, and not a number
                    }
                    int i = at;
                    while (i < len && is_word(g, line[i])) {
                        i++;
                    }
                    took = i - at;
                    cls = (tok_class) r->cls;
                } break;
                case M_NUMBER: {
                    if (at > 0 && is_word(g, line[at - 1])) {
                        break;      // the tail of a word is not a number
                    }
                    const int k2 = number_at(g, line, len, at);
                    if (k2 > 0) {
                        took = k2;
                        cls = (tok_class) r->cls;
                    }
                } break;
                default:
                    break;
            }
        }

        if (took == 0) {
            // Nothing claimed this byte. A whole word goes at once so that the
            // next position is a boundary again, which is what lets the word
            // rules trust `is_word(g, line[at - 1])`.
            took = 1;
            if (is_word(g, line[at])) {
                while (at + took < len && is_word(g, line[at + took])) {
                    took++;
                }
            }
            cls = TOK_TEXT;
        }
        if (line[at] != ' ' && line[at] != '\t') {
            at_line_start = false;
        }
        at += took;
        n = add_run(out, n, max, at, cls);
    }

    if (out_state != NULL) {
        *out_state = state;
    }

    return n;
}

bool syn_crosses_lines(const syntax* g) {
    if (g == NULL || !g->loaded) {
        return false;
    }
    for (int k = 0; k < g->nrules; k++) {
        if (g->rules[k].kind == M_SPAN && g->rules[k].multiline != 0) {
            return true;
        }
    }

    return false;
}

int syn_state_before(const syntax* g, int y, syn_line_fn get, void* ctx,
                     char* buf, int bufmax) {
    if (g == NULL || !g->loaded || get == NULL || y <= 0 || buf == NULL
            || bufmax <= 0) {
        return SYN_STATE_NONE;
    }

    /*
     * A grammar with nothing that crosses a line can only ever answer NONE, so
     * a jump in an assembly file reads no lines at all. C is the one that pays.
     */
    if (!syn_crosses_lines(g)) {
        return SYN_STATE_NONE;
    }

    int from = y - SYN_LOOKBACK;
    if (from < 0) {
        from = 0;
    }

    int state = SYN_STATE_NONE;
    for (int i = from; i < y; i++) {
        const int n = get(ctx, i, buf, bufmax);
        if (n < 0) {
            break;              // the document ended early; keep what we have
        }
        syn_lex(g, buf, n, state, &state, NULL, 0);
    }

    return state;
}
