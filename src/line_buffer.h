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

#ifndef _LINE_BUFFER_H_
#define _LINE_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _line_buffer  {
    int size_;
    int* buf_;
    int* curr_;
    int* cend_;
} line_buffer;

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size);
void lb_destroy(line_buffer* lb);

// Empties the buffer without freeing it, leaving it as lb_init left it. The
// counterpart of cb_clear, and what lets a document be replaced in place.
void lb_clear(line_buffer* lb);

// Info ops, and the line and cursor ops that are a few instructions each.
//
// These are here rather than in the .c because every traversal in the editor --
// a seek, a search, a range walk, a page down -- calls several of them for each
// line it passes, and a call across a translation unit is a call the compiler
// cannot take away however small the body is. There is no link-time
// optimisation on this toolchain, so the header is where "small enough to
// inline" has to be said.
static inline int lb_curr(line_buffer* lb) {
    return (int) (lb->curr_ - lb->buf_);
}

static inline int lb_avai(line_buffer* lb) {
    return (int) (lb->cend_ - lb->curr_);
}

static inline int lb_max(line_buffer* lb) {
    return lb->size_;
}

static inline bool lb_last(line_buffer* lb) {
    return lb->cend_ == (lb->buf_ + lb->size_);
}

static inline bool lb_cinc(line_buffer* lb) {
    (*lb->curr_) += 1;

    return true;
}

// Adds `n` bytes to the current line at once. What lb_cinc does a byte at a
// time, for when a whole line's worth arrives together.
static inline void lb_cadd(line_buffer* lb, int n) {
    (*lb->curr_) += n;
}

static inline int lb_csize(line_buffer* lb) {
    return *lb->curr_;
}

static inline bool lb_up(line_buffer* lb) {
    const bool ok = lb->curr_ > lb->buf_;
    if (ok) {
        lb->cend_--;
        *lb->cend_ = *lb->curr_;
        lb->curr_--;
    }

    return ok;
}

static inline bool lb_down(line_buffer* lb) {
    const bool ok = lb->cend_ < (lb->buf_ + lb->size_);
    if (ok) {
        lb->curr_++;
        *lb->curr_ = *lb->cend_;
        lb->cend_++;
    }

    return ok;
}

bool lb_cdec(line_buffer* lb);
// True when lb_new has somewhere to put a new line. It needs two slots, not
// one -- see lb_new -- so asking it directly is the only way a caller can know
// whether a split will go through without duplicating that arithmetic.
bool lb_can_new(line_buffer* lb);

/*
 * The four ends of the index, matching the character buffer's, for paging.
 *
 * Whole lines leave one end of memory and arrive at the other, and the index
 * has to move with the text it describes: a line whose bytes have gone to the
 * head has no business still being counted here.
 *
 * The asymmetry to know about is that `curr_` points *at* the current line
 * rather than past it. So take_front can take at most the lines strictly before
 * the cursor -- the line the cursor is on cannot leave memory while the cursor
 * is on it -- and the free slots between the two sides are one fewer than
 * lb_avai reports.
 *
 * take_ returns how many entries it gave, fewer than asked when that end holds
 * less. give_ is all or nothing. See .internal/docs/PAGING.md.
 */
int  lb_take_front(line_buffer* lb, int* out, int n);
bool lb_give_front(line_buffer* lb, const int* in, int n);
int  lb_take_back(line_buffer* lb, int* out, int n);
bool lb_give_back(line_buffer* lb, const int* in, int n);

// Free slots between the two sides, which is what give_ has to fit into. One
// fewer than lb_avai, which counts the current line's own slot as available.
int  lb_room(const line_buffer* lb);

/*
 * How many whole lines at each end fit in `max_bytes`, and how many bytes that
 * actually is. A slide moves whole lines and nothing else: memory then always
 * holds complete lines, and neither the index nor anything reading it needs a
 * case for a line that straddles the edge.
 *
 * Returns the byte total and sets *lines. Both are zero when even the first
 * line at that end is longer than `max_bytes` -- a line longer than a chunk
 * cannot be slid, which is the documented limit that a single line longer than
 * memory cannot be represented at all.
 */
int lb_front_fit(const line_buffer* lb, int max_bytes, int* lines);
int lb_back_fit(const line_buffer* lb, int max_bytes, int* lines);

bool lb_new(line_buffer* lb, int size);
bool lb_del(line_buffer* lb);
bool lb_merge_next(line_buffer* lb);
int lb_merge_prev(line_buffer* lb);

#endif  // _LINE_BUFFER_H_
