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

#include <string.h>
#include "char_buffer.h"

#include <stdlib.h>

char_buffer* cb_init(char_buffer* cb, int size) {
    cb->buf_ = (char*) malloc(sizeof(char) * size);
    if (cb->buf_ == NULL) {
        return NULL;
    }

    cb->size_ = size;
    cb_clear(cb);
    return cb;
}

void cb_destroy(char_buffer* cb) {
    free(cb->buf_);
}

void cb_clear(char_buffer* cb) {
    cb->lo_ = cb->buf_;
    cb->curr_ = cb->buf_;
    cb->cend_ = cb->buf_ + cb->size_;
    cb->hi_ = cb->buf_ + cb->size_;
}

/*
 * Puts the free space where it is wanted: `lo` bytes below the live text, `gap`
 * at the cursor, and whatever is left above. One move of the live bytes, which
 * is what every slide used to cost, and it happens once per (free space at an
 * end / chunk) of them.
 *
 * The gap is not only there for typing. A read-only copy walking the document
 * moves the gap as it goes, and that stays safe for the cursor that owns the
 * buffer only while the gap is wider than the distance the copy has travelled
 * -- memmove(curr_, cend_, n) leaves behind the bytes it read, so the original
 * still sees them, until the writes catch up with where its cend_ points.
 *
 * Before this file had ends, the gap held *all* the free space, so that was
 * true by accident. Asking for a share of it on purpose is what keeps it true,
 * and it is why the buffer has to keep a real part of itself free -- see
 * prime_spare in text_buffer.c.
 */
static void cb_arrange(char_buffer* cb, int lo, int gap) {
    const int psz = (int) (cb->curr_ - cb->lo_);
    const int ssz = (int) (cb->hi_ - cb->cend_);
    const int free_all = cb->size_ - psz - ssz;

    if (gap < 0) {
        gap = 0;
    }
    if (gap > free_all) {
        gap = free_all;
    }
    if (lo < 0) {
        lo = 0;
    }
    if (lo > free_all - gap) {
        lo = free_all - gap;
    }

    char* const new_lo = cb->buf_ + lo;
    char* const new_curr = new_lo + psz;
    char* const new_cend = new_curr + gap;

    // Whichever way the bytes travel, the move that goes first is the one whose
    // destination the other move's source is sitting in.
    if (new_lo > cb->lo_) {
        memmove(new_cend, cb->cend_, (size_t) ssz);
        memmove(new_lo, cb->lo_, (size_t) psz);
    } else {
        memmove(new_lo, cb->lo_, (size_t) psz);
        memmove(new_cend, cb->cend_, (size_t) ssz);
    }

    cb->lo_ = new_lo;
    cb->curr_ = new_curr;
    cb->cend_ = new_cend;
    cb->hi_ = new_cend + ssz;
}

// An even three-way split, for when nothing in particular is being asked for.
static void cb_rebalance(char_buffer* cb) {
    const int free_all = cb_available(cb);

    cb_arrange(cb, free_all / 3, free_all / 3);
}

// Room for `n` at one end, and half of whatever is left over kept at the
// cursor. Always enough when the caller has checked that n fits in the buffer
// at all, which is what makes a give at either end succeed or fail on the one
// question of whether the bytes fit.
static void cb_room_front(char_buffer* cb, int n) {
    const int spare = cb_available(cb) - n;

    cb_arrange(cb, n + (spare > 0 ? spare / 2 : 0), spare > 0 ? spare / 2 : 0);
}

static void cb_room_back(char_buffer* cb, int n) {
    const int spare = cb_available(cb) - n;
    const int gap = spare > 0 ? spare / 2 : 0;

    cb_arrange(cb, spare > 0 ? spare - gap : 0, gap);
}

int cb_size(char_buffer* cb) {
    return cb->size_;
}

int cb_available(char_buffer* cb) {
    return cb->size_ - cb_used(cb);
}

int cb_used(char_buffer* cb) {
    return (int) ((cb->curr_ - cb->lo_) + (cb->hi_ - cb->cend_));
}

// Returns false and writes nothing when the gap is closed, i.e. the buffer is
// full. Callers must not advance their own bookkeeping on a refused write.
bool cb_put(char_buffer* cb, char ch) {
    if (cb->curr_ >= cb->cend_) {
        // The gap is shut. There may still be room at the ends -- a run of
        // slides leaves it there -- so take a share of it back before saying
        // the buffer is full.
        if (cb_available(cb) == 0) {
            return false;
        }
        cb_rebalance(cb);
        if (cb->curr_ >= cb->cend_) {
            return false;
        }
    }
    *cb->curr_ = ch;
    cb->curr_++;

    return true;
}

// The same as cb_put in a loop, in one block move. A copy or a paste hands over
// whole spans, and asking for them a byte at a time is a call, a bounds check
// and a return for each one -- which is what a range copy used to be.
//
// All or nothing: a partial write would leave the caller having to work out how
// much landed, and every caller here treats a short write as a failure anyway.
bool cb_write(char_buffer* cb, const char* buf, int sz) {
    if (sz <= 0) {
        return true;
    }
    if (sz > (int) (cb->cend_ - cb->curr_)) {
        if (sz > cb_available(cb)) {
            return false;
        }
        cb_rebalance(cb);           // see cb_put
        if (sz > (int) (cb->cend_ - cb->curr_)) {
            cb_arrange(cb, 0, sz);  // everything the gap can have
            if (sz > (int) (cb->cend_ - cb->curr_)) {
                return false;
            }
        }
    }
    memmove(cb->curr_, buf, (size_t) sz);
    cb->curr_ += sz;

    return true;
}

int cb_take_front(char_buffer* cb, char* out, int n) {
    if (cb == NULL || out == NULL || n <= 0) {
        return 0;
    }
    const int have = (int) (cb->curr_ - cb->lo_);
    if (n > have) {
        n = have;
    }
    if (n == 0) {
        return 0;
    }
    memcpy(out, cb->lo_, (size_t) n);
    cb->lo_ += n;       // the space it gives up joins the free end below

    return n;
}

bool cb_give_front(char_buffer* cb, const char* in, int n) {
    if (cb == NULL || in == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (n > cb_available(cb)) {
        return false;       // nowhere in the buffer for it
    }
    if (n > (int) (cb->lo_ - cb->buf_)) {
        cb_room_front(cb, n);
    }
    cb->lo_ -= n;
    memcpy(cb->lo_, in, (size_t) n);

    return true;
}

int cb_take_back(char_buffer* cb, char* out, int n) {
    if (cb == NULL || out == NULL || n <= 0) {
        return 0;
    }
    const int have = (int) (cb->hi_ - cb->cend_);
    if (n > have) {
        n = have;
    }
    if (n == 0) {
        return 0;
    }
    cb->hi_ -= n;
    memcpy(out, cb->hi_, (size_t) n);

    return n;
}

bool cb_give_back(char_buffer* cb, const char* in, int n) {
    if (cb == NULL || in == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (n > cb_available(cb)) {
        return false;
    }
    if (n > (int) (cb->buf_ + cb->size_ - cb->hi_)) {
        cb_room_back(cb, n);
    }
    memcpy(cb->hi_, in, (size_t) n);
    cb->hi_ += n;

    return true;
}

bool cb_del(char_buffer* cb) {
    const bool ok = cb->cend_ < cb->hi_;
    if (ok) {
        cb->cend_++;
    }
    return ok;
}

bool cb_bksp(char_buffer* cb) {
    const bool bk = cb->curr_ > cb->lo_;
    if (bk) {
        cb->curr_--;
    }
    return bk;
}

char cb_prev(char_buffer* cb, int cnt) {
    const bool pr = cb->curr_ > cb->lo_;
    if (!pr) {
        return 0;
    }

    // One block move rather than a byte at a time. The eZ80 has LDIR and LDDR,
    // and the compiler reaches them through memmove and nothing else -- a hand
    // written byte loop gets a byte loop. Moving the gap is what a search, a
    // page down and a seek all spend their time on, so this is most of what
    // walking a document costs.
    //
    // memmove rather than memcpy: the two sides are separated by the gap and so
    // do not normally overlap, but a gap smaller than the move is legal and the
    // overlap is real when it happens.
    int n = cnt;
    const int have = (int) (cb->curr_ - cb->lo_);
    if (n > have) {
        n = have;
    }
    if (n > 0) {
        cb->cend_ -= n;
        cb->curr_ -= n;
        memmove(cb->cend_, cb->curr_, (size_t) n);
    }

    // Guarded the way cb_next guards its own read. A count of zero skips the
    // loop entirely, so with the cursor at the very end of the buffer cend_ is
    // still one past the last byte and reading it walks off the allocation.
    // tb_home does exactly that -- cb_prev(cb, 0) -- whenever HOME is pressed
    // with the cursor already at the start of the last line.
    if (cb->cend_ < cb->hi_) {
        return *cb->cend_;
    }

    return 0;
}

char cb_next(char_buffer* cb, int cnt) {
    const char* end = cb->hi_;
    if (cb->cend_ >= end) {
        return 0;
    }

    // One block move; see the note in cb_prev.
    int n = cnt;
    const int have = (int) (end - cb->cend_);
    if (n > have) {
        n = have;
    }
    if (n > 0) {
        memmove(cb->curr_, cb->cend_, (size_t) n);
        cb->curr_ += n;
        cb->cend_ += n;
    }

    if (cb->cend_ < end) {
        return *cb->cend_;
    }
    return 0;
}

char cb_peek(char_buffer* cb) {
    if (cb->cend_ == cb->hi_) {
        return 0;
    }
    return *cb->cend_;
}

char* cb_prefix(char_buffer* cb, int* sz) {
    *sz = (int) (cb->curr_ - cb->lo_);
    if (*sz == 0) {
        return NULL;
    }
    return cb->lo_;
}

char* cb_suffix(char_buffer* cb, int* sz) {
    *sz = (int) (cb->hi_ - cb->cend_);
    if (*sz == 0) {
        return NULL;
    }
    return cb->cend_;
}
