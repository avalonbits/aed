#include "text_buffer.h"

text_buffer* tb_init(text_buffer* tb, int mem_kb) {
    if (!cb_init(&tb->cb_, mem_kb)) {
        return NULL;
    }
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
}
bool tb_bksp(text_buffer* tb) {
    return cb_bksp(&tb->cb_);
}

// Cursor ops.
uint8_t tb_next(text_buffer* tb) {
    return cb_next(&tb->cb_);
}
uint8_t tb_prev(text_buffer* tb) {
    return cb_prev(&tb->cb_);
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
