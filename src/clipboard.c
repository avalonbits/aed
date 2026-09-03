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

#include "clipboard.h"

#include <stddef.h>

clipboard* clip_init(clipboard* c, int size) {
    if (cb_init(&c->buf_, size) == NULL) {
        return NULL;
    }
    c->lines_ = 0;

    return c;
}

void clip_destroy(clipboard* c) {
    cb_destroy(&c->buf_);
    c->lines_ = 0;
}

bool clip_has(clipboard* c) {
    // Derived rather than tracked. A flag alongside the buffer is a second
    // account of the same fact, and the two can disagree.
    return clip_size(c) > 0;
}

int clip_size(clipboard* c) {
    int sz = 0;
    cb_prefix(&c->buf_, &sz);

    return sz;
}

int clip_lines(clipboard* c) {
    return c->lines_;
}

int clip_capacity(clipboard* c) {
    return cb_size(&c->buf_);
}

bool clip_copy(clipboard* c, text_buffer* tb, tb_pos a, tb_pos b) {
    const int n = tb_range_copy(tb, a, b, &c->buf_);
    if (n < 0) {
        // Did not fit. tb_range_copy leaves the buffer empty rather than part
        // full, and the old contents are gone either way, so the count of what
        // is in it has to go with them.
        c->lines_ = 0;

        return false;
    }

    int sz = 0;
    const char* text = cb_prefix(&c->buf_, &sz);
    c->lines_ = 0;
    for (int i = 0; i < sz; i++) {
        if (text[i] == '\n') {
            c->lines_++;
        }
    }
    return n > 0;
}

bool clip_paste(clipboard* c, text_buffer* tb) {
    int sz = 0;
    char* text = cb_prefix(&c->buf_, &sz);
    if (text == NULL || sz <= 0) {
        return false;
    }

    return tb_insert(tb, text, sz);
}
