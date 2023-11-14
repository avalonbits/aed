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

#include <stdlib.h>
#include <stdio.h>

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size) {
    lb->buf_ = (int*) malloc(size * sizeof(int));
    if (lb->buf_ == NULL) {
        return NULL;
    }
    for (int i = 0; i < size; ++i) {
        lb->buf_[i] = 0;
    }
    lb->size_ = size;
    lb->curr_ = lb->buf_;
    lb->cend_ = lb->buf_ + size;
    return lb;
}

void lb_destroy(line_buffer* lb) {
    free(lb->buf_);
    lb->buf_ = NULL;
    lb->curr_ = NULL;
    lb->cend_ = NULL;
}

// Info ops
int lb_curr(line_buffer* lb) {
    return lb->curr_ - lb->buf_;
}
int lb_avai(line_buffer* lb) {
    return lb->cend_ - lb->curr_;
}
int lb_max(line_buffer* lb) {
    return lb->size_;
}
bool lb_last(line_buffer* lb) {
    return lb->cend_ == (lb->buf_ + lb->size_);
}

// Line ops.
bool lb_cinc(line_buffer* lb) {
    *lb->curr_ = *lb->curr_ + 1;
    return true;
}
bool lb_cdec(line_buffer* lb) {
    int cur = *lb->curr_;
    if (cur > 0) {
        (*lb->curr_) = cur - 1;
        return true;
    }
    return false;
}
int lb_csize(line_buffer* lb) {
    return *lb->curr_;
}

// Cursor ops.
bool lb_up(line_buffer* lb) {
    bool ok = lb->curr_ > lb->buf_;
    if (ok) {
        lb->cend_--;
        *lb->cend_ = *lb->curr_;
        lb->curr_--;
    }
    return ok;
}
bool lb_down(line_buffer* lb) {
    bool ok = lb->cend_ < (lb->buf_+lb->size_);
    if (ok) {
        lb->curr_++;
        *lb->curr_ = *lb->cend_;
        lb->cend_++;
    }
    return ok;
}
bool lb_new(line_buffer* lb, int size) {
    bool ok = lb->curr_ < lb->cend_;
    if (ok) {
        const int csz = *lb->curr_;
        *lb->curr_ = size;
        lb->curr_++;
        *lb->curr_ = (csz - size);
    }
    return ok;
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

    int curr = *lb->curr_;
    lb->curr_--;
    (*lb->curr_) -= 2;
    int next = *lb->curr_;
    (*lb->curr_) += curr;

    return next;
}


int lb_copy(line_buffer* lb, uint8_t* buf, int size) {
    const int prefix = lb->curr_ - lb->buf_+1;
    int used;
    for (used = 0; used < size && used < prefix; used++) {
        buf[used] = lb->buf_[used];
    }

    const int suffix = (lb->buf_+lb->size_) - lb->cend_;
    for (int i = 0; i < suffix && used < size; used++,i++) {
        buf[used] = lb->cend_[i];
    }

    return used;
}

