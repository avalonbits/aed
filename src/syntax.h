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
 * What a grammar can say about a piece of text, and what a theme colours.
 *
 * Deliberately few. A grammar names TextMate scopes -- `keyword.control`,
 * `string.quoted.double`, `comment.block` -- because that is what makes a
 * grammar recognisable and what a converter from a real .sublime-syntax would
 * produce. Those collapse onto the classes below by their first component, so
 * a theme is a dozen bytes rather than a table of every scope anyone has ever
 * invented, and a grammar naming a scope AED has never heard of renders as
 * ordinary text instead of failing to load.
 *
 * See .internal/docs/SYNTAX.md.
 */
#ifndef _SYNTAX_H_
#define _SYNTAX_H_

#include <stdbool.h>

typedef enum _tok_class {
    TOK_TEXT = 0,       // anything a grammar did not claim
    TOK_COMMENT,        // comment.*
    TOK_STRING,         // string.*
    TOK_NUMBER,         // constant.numeric*
    TOK_KEYWORD,        // keyword.* except the preprocessor
    TOK_TYPE,           // storage.*, entity.name.type
    TOK_PREPROC,        // keyword.control.preprocessor
    TOK_LABEL,          // entity.name.*
    TOK_OPERATOR,       // keyword.operator, punctuation.*
    TOK_N               // how many there are; not a class
} tok_class;

// Which class a TextMate scope name belongs to. Matches on the first component
// and falls back to TOK_TEXT, which is what makes an unknown scope harmless.
tok_class syn_class_of(const char* scope, int len);

// The name a theme file uses for a class, for reading and writing one.
const char* syn_class_name(tok_class c);

/*
 * A theme: one colour per class, and the pair the document is drawn on.
 *
 * A theme is keyed to the background it was designed against, because a colour
 * that reads well on black is unreadable on white. `covers` is the list of
 * background indices its author says it suits; AED picks the first theme that
 * covers the background in force.
 *
 * fg and bg are what the theme wants the document drawn in, and they move the
 * screen's *active* pair only -- see scr_theme_scheme. The pair the user chose
 * is never touched by a theme.
 */
/*
 * A row's colouring, as runs. Run i covers the columns from where run i-1 ended
 * up to `end`, so a row is a handful of these rather than a colour per column.
 * That is also the shape the painting wants: it walks columns upwards and only
 * emits a colour change when it crosses a run boundary.
 */
typedef struct _tok_run {
    int end;            // one past the last column this run covers
    char cls;           // a tok_class
} tok_run;

#define THEME_MAX_COVERS 16
#define THEME_NAME_MAX   16

typedef struct _theme {
    char name[THEME_NAME_MAX];
    char colour[TOK_N];             // a colour index per class
    char fg;                        // the document's own text colour, or -1
    char bg;                        // the background the theme wants, or -1
    char covers[THEME_MAX_COVERS];  // background indices this theme suits
    char ncovers;
    bool loaded;
} theme;

// Empties a theme: every class TOK_TEXT's colour, nothing covered.
void theme_clear(theme* t);

// Reads an INI theme. False leaves the theme untouched, so a bad file gives
// the previous theme rather than a broken one.
bool theme_load(theme* t, const char* path);

// Whether this theme's author meant it for that background.
bool theme_covers(const theme* t, int bg);

// The colour for a class. TOK_TEXT when the theme says nothing about it.
char theme_colour(const theme* t, tok_class c);

/*
 * A grammar: how a language's text divides into the classes above.
 *
 * Sublime's model, without its regular expressions. A rule is a way of
 * matching, a scope for what it matched, and that is all -- there is no engine
 * here, because an eZ80 cannot run one per column per repaint (see
 * .internal/docs/SYNTAX.md, and test/probes/vducost.c for what a repaint
 * costs). Six ways of matching cover assembly, C and BASIC completely.
 */
typedef enum _match_kind {
    M_EOL = 0,      // a literal, and the rest of the line after it
    M_SPAN,         // from one literal to another, with an optional escape
    M_WORDS,        // any of a set, on whole-word boundaries
    M_BOL,          // a literal, only where a line starts
    M_NUMBER,       // a numeric literal; built in, every language wants one
    M_LABEL,        // an identifier where a line starts
} match_kind;

#define SYN_LIT_MAX    4        // "/*", "//", ";" -- none of them are long
#define SYN_MAX_RULES  12
#define SYN_WORDS_MAX  768      // the packed text of every word set
#define SYN_WORDOFF_MAX 192     // one offset per word, sorted for searching

typedef struct _syn_rule {
    char kind;                  // a match_kind
    char cls;                   // a tok_class, from the scope the rule named
    char open[SYN_LIT_MAX];
    char nopen;
    char close[SYN_LIT_MAX];
    char nclose;
    char escape;                // 0 for none
    int word_at;                // first offset in the grammar's word index
    int word_n;
} syn_rule;

#define SYN_NAME_MAX 24
#define SYN_EXTS_MAX 64

typedef struct _syntax {
    char name[SYN_NAME_MAX];
    char exts[SYN_EXTS_MAX];    // ".c .h .cc", as written
    bool nocase;
    syn_rule rules[SYN_MAX_RULES];
    int nrules;
    char words[SYN_WORDS_MAX];  // every word set, packed and NUL-terminated
    int nwords;                 // bytes used
    int wordoff[SYN_WORDOFF_MAX];
    int noffs;
    bool loaded;
} syntax;

// Empties a grammar. A cleared one matches nothing, which paints plainly.
void syn_clear(syntax* g);

// Reads an INI grammar. False leaves it untouched, so a bad file gives the
// grammar already loaded rather than half of a new one.
bool syn_load(syntax* g, const char* path);

// Whether this grammar claims a file name, by its extension.
bool syn_covers(const syntax* g, const char* fname);

/*
 * Colours one line, as runs.
 *
 * Writes at most `max` runs and returns how many. Columns are byte offsets
 * into the line, which is what the painting counts in before tabs are
 * expanded. Runs come out in order and cover the line end to end, so the
 * painting can walk them forward without searching.
 *
 * A line is lexed on its own: nothing here carries state from the line above,
 * which is why a grammar in this step has no rule that crosses a line.
 */
int syn_lex(const syntax* g, const char* line, int len, tok_run* out, int max);

#endif  // _SYNTAX_H_
