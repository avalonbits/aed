#include "text_buffer.h"

text_buffer* tb_init(text_buffer* tb, int mem_kb) {
    int line_count = mem_kb << 5;
    int char_count = (mem_kb << 10) - line_count;
    if (!cb_init(&tb->cb_, char_count)) {
        return NULL;
    }
    if (!lb_init(&tb->lb_, line_count)) {
        cb_destroy(&tb->cb_);
        return NULL;
    }
    tb->x_ = 1;
    tb->y_ = 1;
    return tb;
}

void tb_destroy(text_buffer* tb) {
    cb_destroy(&tb->cb_);
}

// Info ops.
int tb_size(text_buffer* tb) {
    return cb_size(&tb->cb_);
}
int tb_available(text_buffer* tb) {
    return cb_available(&tb->cb_);
}
int tb_used(text_buffer* tb) {
    return cb_used(&tb->cb_);
}

// Character ops.
void tb_put(text_buffer* tb, uint8_t ch) {
    cb_put(&tb->cb_, ch);
    tb->x_++;
}
bool tb_bksp(text_buffer* tb) {
    const bool ok = cb_bksp(&tb->cb_);
    if (ok) {
        tb->x_--;
    }
    return ok;
}

// Cursor ops.
uint8_t tb_next(text_buffer* tb) {
    const bool ok = cb_next(&tb->cb_);
    if (ok) {
        tb->x_++;
    }
    return ok;
}
uint8_t tb_prev(text_buffer* tb) {
    const bool ok = cb_prev(&tb->cb_);
    if (ok) {
        tb->x_--;
    }
    return ok;
}

int tb_xpos(text_buffer* tb) {
    return tb->x_;
}

int tb_ypos(text_buffer* tb) {
    return tb->y_;
}

// Char read.
uint8_t tb_peek(text_buffer* tb) {
    return cb_peek(&tb->cb_);
}
uint8_t tb_peek_at(text_buffer* tb, int idx) {
    return cb_peek_at(&tb->cb_, idx);
}
int tb_copy(text_buffer* tb, uint8_t* buf, int sz) {
    return cb_copy(&tb->cb_, buf, sz);
}
uint8_t* tb_suffix(text_buffer* tb, int* sz) {
    return cb_suffix(&tb->cb_, sz);
}

#
