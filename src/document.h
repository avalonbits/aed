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

#ifndef _DOCUMENT_H_
#define _DOCUMENT_H_

#include <stdbool.h>

#include "syntax.h"
#include "text_buffer.h"
#include "undo.h"

/*
 * One open file, and everything that belongs to it rather than to the screen
 * it is shown on: the text, its undo log, the grammar it is written in, what
 * each line begins inside, and the selection.
 *
 * The program holds one of these per open file. Nothing here knows about the
 * VDP, the keyboard or where on screen the document is drawn; a view (the
 * cursor row, the scroll, which line is at the top) is kept by whoever shows
 * it.
 */

// The most rows a view can show. The syntax window below holds a screenful
// beyond what a jump reads back, and a screen can be no taller than this --
// the front end asserts its own limit against it.
#define DOC_ROWS_MAX 96

#define SYN_WINDOW (SYN_LOOKBACK + DOC_ROWS_MAX)

typedef struct _document {
    text_buffer buf_;

    // Session state for this document: it does not outlive it, and opening
    // another file into it clears it.
    undo undo_;

    /*
     * The grammar the document is written in, chosen by its name. Cleared
     * when no grammar claims the file, which paints it plainly.
     *
     * The theme that colours it is the screen's rather than the document's:
     * it is chosen by the background, and every document on that screen is
     * drawn in it. See editor.h.
     */
    syntax syn_;

    /*
     * What each line begins inside.
     *
     * Keyed by document line rather than by screen row, and that is the whole
     * of it. A line begins inside what it begins inside; which row it happens
     * to be drawn on has nothing to do with that. Keyed by row, scrolling threw
     * every answer away although the document had not changed, and the code
     * that chased rows around had cases it could not express and gave up in --
     * after which the cursor had nothing to consult and rubbed the colouring
     * out of each cell it crossed.
     *
     * `synFirst_` is the first line an answer is held for and `synKnown_` how
     * many consecutive lines from it. So the answers are a window that grows
     * downwards when a line below it is asked about and is refilled when one
     * above it is. Zero is a starting point rather than a failure: there is no
     * state this can be left in that means give up.
     *
     * Only a grammar that crosses lines needs any of it, and only C does so
     * far. Without it, painting one row means lexing every row above to find
     * out what it is inside -- 28 rows on a half-screen cursor, which measured
     * 45 milliseconds a keystroke on an Agon.
     *
     * How wide it is follows from what fills it. The one expensive thing here
     * is the read-back a view does when it lands somewhere it has no answer
     * for, and that walk settles SYN_LOOKBACK lines whether or not there is
     * anywhere to put them. A window narrower than the walk throws the rest
     * away and reads them again on the next page up; a window wider than the
     * walk holds space nothing can fill. So: the walk, and a screen below it
     * for the rows the view is about to paint.
     */
    char lineSyn_[SYN_WINDOW];
    int synFirst_;
    int synKnown_;

    // Where the selection started, and whether there is one. Held beside the
    // buffer rather than in it, because it is about intent: the text has no
    // idea any of this is happening, and a selection means nothing once the
    // file changes. Per document on purpose, so two views of one file share
    // a selection; a view that wants its own would carry these instead.
    tb_pos anchor_;
    bool selecting_;
} document;

#endif  // _DOCUMENT_H_
