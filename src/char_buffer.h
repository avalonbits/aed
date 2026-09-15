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

#ifndef _CHAR_BUFFER_H_
#define _CHAR_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * A gap buffer with free space at *both* ends:
 *
 *   [ free ][ prefix ][ gap ][ suffix ][ free ]
 *   ^buf_   ^lo_      ^curr_ ^cend_    ^hi_    ^buf_+size_
 *
 * The gap at the cursor makes typing cheap. The free space outside lo_ and hi_
 * makes the four ends cheap, and those are what a window sliding over a large
 * document uses. Without it, taking a chunk off one end closed that whole side
 * up against the buffer, so every 2 KB slide moved a quarter of a megabyte --
 * measured as 71% of what walking a 419 KB document cost, against 4% for every
 * read and write to the card.
 *
 * Sliding one way eats the free space at one end and makes it at the other, so
 * the two are evened up when a side runs dry. That costs one move of the live
 * bytes, which is what every slide used to cost.
 */
typedef struct _char_buffer  {
    int size_;
    char* buf_;     // the allocation
    char* lo_;      // where the live bytes start
    char* curr_;    // end of the prefix: the cursor
    char* cend_;    // start of the suffix
    char* hi_;      // where the live bytes end
} char_buffer;

// How much lies either side of the cursor, without the pointer that goes with
// it. Here rather than in the .c for the reason line_buffer.h gives for its
// own accessors: settling asks for both on every cursor movement of a paged
// document -- twice over, because tb_up and tb_down each settle -- and throws
// the pointers away. There is no link-time optimisation on this toolchain, so
// a call across a translation unit stays a call however small the body is.
static inline int cb_prefix_size(const char_buffer* cb) {
    return (int) (cb->curr_ - cb->lo_);
}

static inline int cb_suffix_size(const char_buffer* cb) {
    return (int) (cb->hi_ - cb->cend_);
}

// Setup ops.
char_buffer* cb_init(char_buffer* cb, int size);
void cb_destroy(char_buffer* cb);
void cb_clear(char_buffer* cb);

// Info ops.
int cb_size(char_buffer* cb);
int cb_available(char_buffer* cb);
int cb_used(char_buffer* cb);

// Character ops.
bool cb_put(char_buffer* cb, char ch);

// A whole span at once, which is what a copy and a paste actually have. Fails
// without writing anything if it does not all fit.
bool cb_write(char_buffer* cb, const char* buf, int sz);

/*
 * The four ends of the buffer, for paging.
 *
 * A slide moves the window a document is seen through: bytes leave one end into
 * a file and arrive at the other end from one. The cursor does not move and the
 * gap does not change size -- what changes is which part of the document the
 * buffer is holding.
 *
 * Front and back are the document's, not the gap's. The front is the first byte
 * of the prefix and the back is the last byte of the suffix, so taking from the
 * front is taking the oldest text and giving to the back is taking on newer.
 *
 * Each is one block move and one copy: the side that is not being touched stays
 * where it is, and the side that is shifts by n to keep the buffer packed
 * against its ends. A slide therefore moves about a bufferful between them,
 * which is roughly a third of what the slide's disk traffic costs.
 *
 * take_ returns how many bytes it actually gave, which is fewer than asked for
 * when that end holds less. give_ is all or nothing and fails when the gap
 * cannot cover it.
 *
 * **Neither take_ reaches past the cursor.** take_front takes from the prefix
 * and stops when it runs out, even if the suffix has more; take_back takes from
 * the suffix and stops the same way. That is not a shortcut -- reaching past
 * the cursor would put it outside the text the buffer is holding, which is not
 * a position the rest of the editor can express. A caller that finds it got
 * fewer bytes than it asked for has a cursor too close to that end to slide,
 * and the margins exist so that does not happen. See .internal/docs/PAGING.md.
 */
int  cb_take_front(char_buffer* cb, char* out, int n);
bool cb_give_front(char_buffer* cb, const char* in, int n);
int  cb_take_back(char_buffer* cb, char* out, int n);
bool cb_give_back(char_buffer* cb, const char* in, int n);
bool cb_del(char_buffer* cb);
bool cb_bksp(char_buffer* cb);

// Cursor ops.
char cb_next(char_buffer* cb, int cnt);
char cb_prev(char_buffer* cb, int cnt);

// Char read.
char cb_peek(char_buffer* cb);
char* cb_prefix(char_buffer* cb, int* sz);
char* cb_suffix(char_buffer* cb, int* sz);


#endif  // _CHAR_BUFFER_H_
