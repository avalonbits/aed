#include "line_buffer.h"

#include <stdlib.h>
#include <stdio.h>

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size) {
    lb->buf_ = (uint8_t*) malloc(size * sizeof(uint8_t));
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
    return lb->cend_ - lb->buf_;
}
const bool lb_last(line_buffer* lb) {
    return lb->cend_ == (lb->buf_ + lb->size_);
}

// Line ops.
bool lb_cinc(line_buffer* lb) {
    uint8_t cur = *lb->curr_;
    if (cur < 0xFF) {
        (*lb->curr_) = cur + 1;
        return true;
    }
    return false;
}
bool lb_cdec(line_buffer* lb) {
    uint8_t cur = *lb->curr_;
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
    const bool ok = lb->curr_ > lb->buf_;
    if (ok) {
        lb->curr_--;
        lb->cend_--;
        *lb->cend_-- = *lb->curr_;
    }
    return ok;
}
bool lb_down(line_buffer* lb) {
    const bool ok = lb->cend_ < (lb->buf_+lb->size_);
    if (ok) {
        *lb->curr_ = *lb->cend_;
        lb->curr_++;
        lb->cend_++;
    }
    return ok;
}
bool lb_new(line_buffer* lb, uint8_t size) {
    const bool ok = lb->curr_ < lb->cend_;
    if (ok) {
        *lb->curr_ = size;
        lb->curr_++;
    }
    return ok;
}


