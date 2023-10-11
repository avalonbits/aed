#include "line_buffer.h"

#include <stdlib.h>
#include <stdio.h>

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size) {
    lb->buf_ = (uint8_t*) calloc(size, sizeof(uint8_t));
    if (lb->buf_ == NULL) {
        return NULL;
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
int lb_size(line_buffer* lb) {
}
int lb_available(line_buffer* lb) {
}
int lb_used(line_buffer* lb) {
}

// Line ops.
bool lb_linc(line_buffer* lb) {
    uint8_t cur = *lb->buf_;
    if (cur < 0xFF) {
        (*lb->buf_) = cur + 1;
        return true;
    }
    return false;
}
bool lb_ldec(line_buffer* lb) {
    uint8_t cur = *lb->buf_;
    if (cur > 0) {
        (*lb->buf_) = cur - 1;
        return true;
    }
    return false;
}
int lb_lcur(line_buffer* lb) {
    return lb->curr_ - lb->buf_;
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
bool lb_new(line_buffer* lb) {
    const bool ok = lb->curr_ < lb->cend_;
    if (ok) {
        lb->curr_++;
    }
    return ok;
}


