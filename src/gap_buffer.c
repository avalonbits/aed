#include "gap_buffer.h"

#include <stdlib.h>

gap_buffer* gb_init(gap_buffer* gb, int size) {
    if (gb == NULL) {
        return NULL;
    }

    gb->buf_ = (uint8_t*) malloc(sizeof(uint8_t) * size);
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
    uint8_t* end = gb->buf_ + gb->size_;
    int total = 0;

    total += (gb->curr_ - gb->buf_);
    total += (end - gb->cend_);
    return total;
}

void gb_put(gap_buffer* gb, uint8_t ch) {
    *gb->curr_ = ch;
    gb->curr_++;
}

bool gb_bksp(gap_buffer* gb) {
    const bool bk = gb->curr_ > gb->buf_;
    if (bk) {
        gb->curr_--;
    }
    return bk;
}

uint8_t gb_prev(gap_buffer* gb) {
    const bool pr = gb->curr_ > gb->buf_;
    if (pr) {
        gb->cend_--;
        gb->curr_--;
        *gb->cend_ = *gb->curr_;
        return *gb->cend_;
    }
    return 0;
}

uint8_t gb_next(gap_buffer* gb) {
    if (gb->cend_ > gb->buf_+gb->size_) {
        return 0;
    }
    uint8_t ch = *gb->curr_ = *gb->cend_;
    gb->curr_++;
    gb->cend_++;
    return ch;
}

uint8_t gb_peek(gap_buffer* gb) {
    const uint8_t* end = gb->buf_ + gb->size_;
    if (gb->curr_ == gb->buf_ || gb->cend_ == end) {
        return 0;
    }
    return *gb->curr_;
}

uint8_t gb_peek_at(gap_buffer* gb, int idx) {
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

int gb_copy(gap_buffer* gb, uint8_t* buf, int size) {
    const int prefix = gb->curr_-gb->buf_;
    int used;
    for (used = 0; used < size && used < prefix; used++) {
        buf[used] = gb->buf_[used];
    }

    const int suffix = (gb->buf_+gb->size_) - gb->cend_;
    for (int i = 0; i < suffix && used < size; used++,i++) {
        buf[used] = gb->cend_[i];
    }

    return used;
}
