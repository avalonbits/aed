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
    lb_destroy(&tb->lb_);
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
    lb_linc(&tb->lb_);
}

bool tb_del(text_buffer* tb) {
    if (cb_del(&tb->cb_)) {
        lb_ldec(&tb->lb_);
    }
}

bool tb_bksp(text_buffer* tb) {
    const bool ok = cb_bksp(&tb->cb_);
    if (ok) {
        tb->x_--;
        lb_ldec(&tb->lb_);
    }
    return ok;
}

bool tb_enter(text_buffer* tb) {
    tb_put(tb, '\r');
    tb_put(tb, '\n');
}

// Cursor ops.
uint8_t tb_next(text_buffer* tb) {
    tb->x_++;
    return cb_next(&tb->cb_);
}
uint8_t tb_prev(text_buffer* tb) {
    const uint8_t ch = cb_prev(&tb->cb_);
    if (ch) {
        tb->x_--;
    }
    return ch;
}

uint8_t tb_home(text_buffer* tb) {
    tb->x_ = 1;
    return cb_home(&tb->cb_);
}

void tb_end(text_buffer* tb) {
}


int tb_xpos(text_buffer* tb) {
    return tb->x_;
}

int tb_ypos(text_buffer* tb) {
    return lb_lcur(&tb->lb_)+1;
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
