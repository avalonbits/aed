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

#endif  // _SYNTAX_H_
