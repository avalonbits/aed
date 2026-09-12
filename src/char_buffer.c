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
    cb->curr_ = cb->buf_;
    cb->cend_ = cb->buf_ + cb->size_;
}

int cb_size(char_buffer* cb) {
    return cb->size_;
}

int cb_available(char_buffer* cb) {
    return cb->size_ - cb_used(cb);
}

int cb_used(char_buffer* cb) {
    char* end = cb->buf_ + cb->size_;
    int total = 0;

    total += (cb->curr_ - cb->buf_);
    total += (end - cb->cend_);
    return total;
}

// Returns false and writes nothing when the gap is closed, i.e. the buffer is
// full. Callers must not advance their own bookkeeping on a refused write.
bool cb_put(char_buffer* cb, char ch) {
    if (cb->curr_ >= cb->cend_) {
        return false;
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
        return false;
    }
    memmove(cb->curr_, buf, (size_t) sz);
    cb->curr_ += sz;

    return true;
}

int cb_take_front(char_buffer* cb, char* out, int n) {
    if (cb == NULL || out == NULL || n <= 0) {
        return 0;
    }
    const int have = (int) (cb->curr_ - cb->buf_);
    if (n > have) {
        n = have;
    }
    if (n == 0) {
        return 0;
    }
    memcpy(out, cb->buf_, (size_t) n);
    // What is left of the prefix closes up against the start of the buffer. The
    // space it gives up joins the gap, which is where the arriving bytes at the
    // other end will come out of.
    memmove(cb->buf_, cb->buf_ + n, (size_t) (have - n));
    cb->curr_ -= n;

    return n;
}

bool cb_give_front(char_buffer* cb, const char* in, int n) {
    if (cb == NULL || in == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (n > (int) (cb->cend_ - cb->curr_)) {
        return false;       // the gap cannot cover it
    }
    const int have = (int) (cb->curr_ - cb->buf_);
    memmove(cb->buf_ + n, cb->buf_, (size_t) have);
    memcpy(cb->buf_, in, (size_t) n);
    cb->curr_ += n;

    return true;
}

int cb_take_back(char_buffer* cb, char* out, int n) {
    if (cb == NULL || out == NULL || n <= 0) {
        return 0;
    }
    char* const top = cb->buf_ + cb->size_;
    const int have = (int) (top - cb->cend_);
    if (n > have) {
        n = have;
    }
    if (n == 0) {
        return 0;
    }
    memcpy(out, top - n, (size_t) n);
    // The rest of the suffix stays packed against the top of the buffer, so it
    // moves up by what was taken off its end.
    memmove(cb->cend_ + n, cb->cend_, (size_t) (have - n));
    cb->cend_ += n;

    return n;
}

bool cb_give_back(char_buffer* cb, const char* in, int n) {
    if (cb == NULL || in == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (n > (int) (cb->cend_ - cb->curr_)) {
        return false;       // the gap cannot cover it
    }
    char* const top = cb->buf_ + cb->size_;
    const int have = (int) (top - cb->cend_);
    memmove(cb->cend_ - n, cb->cend_, (size_t) have);
    cb->cend_ -= n;
    memcpy(top - n, in, (size_t) n);

    return true;
}

bool cb_del(char_buffer* cb) {
    const char* end = cb->buf_+cb->size_;
    const bool ok = cb->cend_ < end;
    if (ok) {
        cb->cend_++;
    }
    return ok;
}

bool cb_bksp(char_buffer* cb) {
    const bool bk = cb->curr_ > cb->buf_;
    if (bk) {
        cb->curr_--;
    }
    return bk;
}

char cb_prev(char_buffer* cb, int cnt) {
    const bool pr = cb->curr_ > cb->buf_;
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
    const int have = (int) (cb->curr_ - cb->buf_);
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
    if (cb->cend_ < cb->buf_ + cb->size_) {
        return *cb->cend_;
    }

    return 0;
}

char cb_next(char_buffer* cb, int cnt) {
    const char* end = cb->buf_+cb->size_;
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
    const char* end = cb->buf_ + cb->size_;
    if (cb->cend_ == end) {
        return 0;
    }
    return *cb->cend_;
}

char* cb_prefix(char_buffer* cb, int* sz) {
    *sz = (cb->curr_ - cb->buf_);
    if (*sz == 0) {
        return NULL;
    }
    return  cb->buf_;
}

char* cb_suffix(char_buffer* cb, int* sz) {
    const char* end = cb->buf_ + cb->size_;
    *sz = end - cb->cend_;
    if (*sz == 0) {
        return NULL;
    }
    return cb->cend_;
}
