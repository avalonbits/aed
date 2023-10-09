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
    printf("%x", lb->cend_-lb->curr_);
    return lb;
}

void lb_destroy(line_buffer* lb) {
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
}
bool lb_ldec(line_buffer* lb) {
}
int lb_lcur(line_buffer* lb) {
}
int lb_lmax(line_buffer* lb) {
}
int lb_lused(line_buffer* lb) {
}

// Cursor ops.
bool lb_up(line_buffer* lb) {
}
bool lb_down(line_buffer* lb) {
}
bool lb_new(line_buffer* lb) {
}


