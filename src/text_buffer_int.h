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
 * What the pieces of the text buffer share with each other.
 *
 * text_buffer.h is the buffer's public face; this is the seam between the
 * files behind it -- text_buffer.c and the text_buffer_*.c beside it. Nothing
 * outside those includes this.
 *
 * The tbi_ prefix says so at every call site: a tb_ name is something the rest
 * of the editor may call, a tbi_ name is one of these files talking to
 * another. They are not static only because they have callers in a second
 * file; each still belongs to the one that defines it, named below.
 */
#ifndef _TEXT_BUFFER_INT_H_
#define _TEXT_BUFFER_INT_H_

#include "text_buffer.h"

#include <stdbool.h>

// What counts as the end of a line. Zero is included: a cursor with nothing
// under it is at the end of one, and several callers lean on that -- the
// sideways movements in cmd_ops.c among them.
#define IS_EOL(x) (x == 0 || (x >= 10 && x <= 13))

// How many bytes this document's line break takes in the buffer. One byte or
// two, decided when the file was read; every edit that removes a break asks.
// Here rather than in a .c because it is a field read and the editor does it
// on every delete, backspace, merge and range.
static inline int eol_len(const text_buffer* tb) {
    return tb->elen_;
}

// --- text_buffer.c ---

// Marks the document as matching what is on disk. Called when it is written,
// and when it is read.
void tbi_saved(text_buffer* tb);

// --- text_buffer_find.c ---

// Line feeds in a run of bytes. Counting on the way past is what lets a paged
// load know how many lines went to the store without reading them again.
int tbi_count_lines(const char* buf, int n);

// Whether a character ends a word, for the steps that move by one.
bool tbi_isstop(char ch);

// --- text_buffer_move.c ---

// The bytes at an offset within the window, as up to two runs -- the gap may
// fall in the middle of what is asked for. Reads without moving anything,
// which is what lets a walker and the range operations share a buffer with the
// cursor that owns it.
void tbi_walk_bytes(text_buffer* tb, int from, int n, char** pre, int* psz,
                    char** suf, int* ssz);

// The length of the line a walker is on.
int tbi_walk_line_len(text_buffer* tb);

// --- text_buffer_page.c ---

// Room kept back when memory is filled at open, so the first slide has
// somewhere to put what it brings in.
int tbi_prime_spare(text_buffer* tb);

// Appends a run to the end of memory, as whole lines, and says how many bytes
// and lines it took. Filling at open and sliding down both end here.
int tbi_mem_give_back(text_buffer* tb, const char* buf, int n, int spare,
                      int* lines);

// Closes the empty entry a fill leaves behind when it started from nothing.
void tbi_mem_close_empty(text_buffer* tb);

// --- text_buffer_io.c ---

// The whole document to a sink, in order: head, memory, tail. Saving needed it
// first; find and the range operations are the rest.
bool tbi_doc_stream(text_buffer* tb, tb_sink sink, void* ctx);

// Lets go of the store, if there is one.
void tbi_drop_store(text_buffer* tb);

#endif  // _TEXT_BUFFER_INT_H_
