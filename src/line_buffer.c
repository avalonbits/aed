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

#include "line_buffer.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size) {
    lb->buf_ = (int*) malloc(size * sizeof(int));
    if (lb->buf_ == NULL) {
        return NULL;
    }

    lb->size_ = size;
    lb_clear(lb);

    return lb;
}

void lb_clear(line_buffer* lb) {
    // The sizes have to go back to zero, not just the pointers: lb_cinc counts
    // a line up one byte at a time from whatever is already in the slot, so a
    // stale length from the last document would be added to rather than
    // replaced, and every line would come out too long.
    memset(lb->buf_, 0, (size_t) lb->size_ * sizeof(int));
    lb->curr_ = lb->buf_;
    lb->cend_ = lb->buf_ + lb->size_;
}

void lb_destroy(line_buffer* lb) {
    free(lb->buf_);
}

// Info ops

// Line ops.
bool lb_cdec(line_buffer* lb) {
    int cur = *lb->curr_;
    if (cur > 0) {
        (*lb->curr_) = cur - 1;
        return true;
    }
    return false;
}

// Cursor ops.
bool lb_can_new(line_buffer* lb) {
    // Two slots: the line being split keeps `size` in the slot it already has,
    // and the remainder is written to the next one. A guard of curr_ < cend_
    // passes with a single slot free and then writes the remainder at cend_ --
    // past the end of the allocation when the gap runs to the end of it.
    return lb->curr_ + 1 < lb->cend_;
}

int lb_room(const line_buffer* lb) {
    const int n = (int) (lb->cend_ - lb->curr_) - 1;

    return n > 0 ? n : 0;
}

int lb_front_fit(const line_buffer* lb, int max_bytes, int* lines) {
    int bytes = 0;
    int n = 0;
    const int have = (int) (lb->curr_ - lb->buf_);
    for (; n < have; n++) {
        const int len = lb->buf_[n];
        if (bytes + len > max_bytes) {
            break;
        }
        bytes += len;
    }
    if (lines != NULL) {
        *lines = n;
    }

    return bytes;
}

int lb_back_fit(const line_buffer* lb, int max_bytes, int* lines) {
    int bytes = 0;
    int n = 0;
    const int* const top = lb->buf_ + lb->size_;
    const int have = (int) (top - lb->cend_);
    for (; n < have; n++) {
        const int len = top[-(n + 1)];
        if (bytes + len > max_bytes) {
            break;
        }
        bytes += len;
    }
    if (lines != NULL) {
        *lines = n;
    }

    return bytes;
}

int lb_take_front(line_buffer* lb, int* out, int n) {
    if (lb == NULL || out == NULL || n <= 0) {
        return 0;
    }
    // Strictly before the cursor: the line it is on goes nowhere.
    const int have = (int) (lb->curr_ - lb->buf_);
    if (n > have) {
        n = have;
    }
    if (n == 0) {
        return 0;
    }
    memcpy(out, lb->buf_, (size_t) n * sizeof(int));
    // Everything up to and including the current line closes up, which is one
    // more entry than were before it.
    memmove(lb->buf_, lb->buf_ + n, (size_t) (have - n + 1) * sizeof(int));
    lb->curr_ -= n;

    return n;
}

bool lb_give_front(line_buffer* lb, const int* in, int n) {
    if (lb == NULL || in == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (n > lb_room(lb)) {
        return false;
    }
    const int have = (int) (lb->curr_ - lb->buf_) + 1;
    memmove(lb->buf_ + n, lb->buf_, (size_t) have * sizeof(int));
    memcpy(lb->buf_, in, (size_t) n * sizeof(int));
    lb->curr_ += n;

    return true;
}

int lb_take_back(line_buffer* lb, int* out, int n) {
    if (lb == NULL || out == NULL || n <= 0) {
        return 0;
    }
    int* const top = lb->buf_ + lb->size_;
    const int have = (int) (top - lb->cend_);
    if (n > have) {
        n = have;
    }
    if (n == 0) {
        return 0;
    }
    memcpy(out, top - n, (size_t) n * sizeof(int));
    memmove(lb->cend_ + n, lb->cend_, (size_t) (have - n) * sizeof(int));
    lb->cend_ += n;

    return n;
}

bool lb_give_back(line_buffer* lb, const int* in, int n) {
    if (lb == NULL || in == NULL || n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (n > lb_room(lb)) {
        return false;
    }
    int* const top = lb->buf_ + lb->size_;
    const int have = (int) (top - lb->cend_);
    memmove(lb->cend_ - n, lb->cend_, (size_t) have * sizeof(int));
    lb->cend_ -= n;
    memcpy(top - n, in, (size_t) n * sizeof(int));

    return true;
}

bool lb_new(line_buffer* lb, int size) {
    if (!lb_can_new(lb)) {
        return false;
    }

    const int csz = *lb->curr_;
    *lb->curr_ = size;
    lb->curr_++;
    *lb->curr_ = (csz - size);

    return true;
}

bool lb_del(line_buffer* lb) {
    if (!lb_last(lb)) {
        *lb->curr_ = *lb->cend_;
        lb->cend_++;
        return true;
    }

    if (*lb->curr_ > 0) {
        lb->curr_ = 0;
        return true;
    }
    return false;
}

bool lb_merge_next(line_buffer* lb) {
    if (lb_last(lb)) {
        return false;
    }

    (*lb->curr_) += *lb->cend_;
    lb->cend_++;
    return true;
}

int lb_merge_prev(line_buffer* lb) {
    if (lb->curr_ == lb->buf_) {
        return -1;
    }

    const int curr = *lb->curr_;
    lb->curr_--;
    (*lb->curr_) -= 2;
    const int next = *lb->curr_;
    (*lb->curr_) += curr;

    return next;
}
