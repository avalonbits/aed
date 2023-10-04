#include "gap_buffer.h"

#include <stdlib.h>

gap_buffer* gb_init(gap_buffer* gb, int size) {
    if (gb == NULL) {
        return NULL;
    }

    gb->buf_ = (CHAR*) malloc(sizeof(CHAR) * size);
    if (gb->buf_ == NULL) {
        return NULL;
    }

    gb->size_ = size;
    gb->curr_ = gb->buf_;
    gb->cend_ = gb->buf_+size;

    return gb;
}

void gb_destroy(gap_buffer* gb) {
    free(gb->buf_);
}

int gb_size(gap_buffer* gb) {
    return gb->size_;
}

int gb_available(gap_buffer* gb) {
    return gb->size_ - gb_used(gb);
}

int gb_used(gap_buffer* gb) {
    CHAR* end = gb->buf_ + gb->size_;
    int total = 0;

    total += (gb->curr_ - gb->buf_);
    total += (end - gb->cend_);
    return total;
}

void gb_put(gap_buffer* gb, CHAR ch) {
    *gb->curr_ = ch;
    gb->curr_++;
}

CHAR gb_peek(gap_buffer* gb) {
    return *(gb->curr_-1);
}

CHAR gb_peek_at(gap_buffer* gb, int idx) {
    if (idx < 0) {
        return '\0';
    }

    const int prefix = gb->curr_-gb->buf_;
    if (idx < prefix) {
        return *(gb->buf_+idx);
    }

    idx -= prefix;
    const int suffix = (gb->buf_+gb->size_) - gb->cend_;
    if (idx < suffix) {
        return *(gb->cend_+idx);
    }

    return '\0';
}

int gb_copy(gap_buffer* gb, CHAR* buf, int size) {
    for (int i = 0; i < size; i++) {
        buf[i] = gb->buf_[i];
    }
    return size;
}
