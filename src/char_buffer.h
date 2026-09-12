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

typedef struct _char_buffer  {
    int size_;
    char* buf_;
    char* curr_;
    char* cend_;
} char_buffer;

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
