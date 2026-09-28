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

#ifndef _VIEW_H_
#define _VIEW_H_

/*
 * A rectangle of the screen that shows a document, and where in it the cursor
 * and the scroll are.
 *
 * The screen is the device -- colours, font, cell size, what the VDP has set --
 * and paints through whichever view it is pointed at. AED has one, the text
 * area of the whole screen, which the screen lays out itself. A program that
 * shows more than one document keeps a view for each and points the screen at
 * the one it is drawing.
 *
 * Rows and columns are screen cells. topY_ is the first text row and bottomY_
 * the row just past the last one, so a view is bottomY_ - topY_ rows tall.
 */
typedef struct _view {
    // Width of the text area, and its first screen column. The header and
    // footer are wider: see barW_ in screen.h.
    int cols_;
    char textX_;

    char topY_;
    char bottomY_;

    // The cursor, as a screen cell inside the rectangle.
    char currX_;
    char currY_;

    // Document column shown at screen column 0. The view scrolls horizontally
    // by moving this rather than by slicing lines at a byte offset, which is
    // what lets one byte occupy more than one column.
    int originX_;

    /*
     * Which document line row topY_ draws. Painting is asked about rows, and
     * this is what turns a row into a line for the document's syntax window
     * -- see document.h. It belongs to the view rather than the document: the
     * same document shown twice would be drawn from two different lines.
     */
    int synTop_;
} view;

#endif  // _VIEW_H_
