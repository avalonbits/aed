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

void cb_put(char_buffer* cb, char ch) {
    *cb->curr_ = ch;
    cb->curr_++;
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

    while (cnt-- > 0 && cb->curr_ > cb->buf_) {
        cb->cend_--;
        cb->curr_--;
        *cb->cend_ = *cb->curr_;
    }
    return *cb->cend_;
}

char cb_next(char_buffer* cb, int cnt) {
    const char* end = cb->buf_+cb->size_;
    if (cb->cend_ >= end) {
        return 0;
    }

    while (cnt-- > 0 && cb->cend_ < end) {
        *cb->curr_ = *cb->cend_;
        cb->curr_++;
        cb->cend_++;
    }

    if (cb->cend_ < end) {
        return *cb->cend_;
    }
    return 0;
}

int cb_pos(char_buffer* cb) {
    return cb->curr_ - cb->buf_;
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
