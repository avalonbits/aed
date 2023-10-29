#include "char_buffer.h"

#include <stdlib.h>

char_buffer* cb_init(char_buffer* cb, int size) {
    cb->buf_ = (uint8_t*) malloc(sizeof(uint8_t) * size);
    if (cb->buf_ == NULL) {
        return NULL;
    }

    cb->size_ = size;
    cb->curr_ = cb->buf_;
    cb->cend_ = cb->buf_+size;

    return cb;
}

void cb_destroy(char_buffer* cb) {
    free(cb->buf_);
}

int cb_size(char_buffer* cb) {
    return cb->size_;
}

int cb_available(char_buffer* cb) {
    return cb->size_ - cb_used(cb);
}

int cb_used(char_buffer* cb) {
    uint8_t* end = cb->buf_ + cb->size_;
    int total = 0;

    total += (cb->curr_ - cb->buf_);
    total += (end - cb->cend_);
    return total;
}

void cb_put(char_buffer* cb, uint8_t ch) {
    *cb->curr_ = ch;
    cb->curr_++;
}

bool cb_del(char_buffer* cb) {
    const uint8_t* end = cb->buf_+cb->size_;
    const bool ok = cb->cend_ < end;
    cb->cend_ += (int)ok;
    return ok;
}

bool cb_bksp(char_buffer* cb) {
    const bool bk = cb->curr_ > cb->buf_;
    if (bk) {
        cb->curr_--;
    }
    return bk;
}

uint8_t cb_prev(char_buffer* cb, int cnt) {
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

uint8_t cb_next(char_buffer* cb, int cnt) {
    const uint8_t* end = cb->buf_+cb->size_;
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

uint8_t cb_peek(char_buffer* cb) {
    const uint8_t* end = cb->buf_ + cb->size_;
    if (cb->cend_ == end) {
        return 0;
    }
    return *cb->cend_;
}

uint8_t cb_peek_at(char_buffer* cb, int idx) {
    if (idx < 0) {
        return 0;
    }

    const int prefix = cb->curr_-cb->buf_;
    if (idx < prefix) {
        return *(cb->buf_+idx);
    }

    idx -= prefix;
    const int suffix = (cb->buf_+cb->size_) - cb->cend_;
    if (idx < suffix) {
        return *(cb->cend_+idx);
    }

    return 0;
}

uint8_t* cb_prefix(char_buffer* cb, int* sz) {
    *sz = (cb->curr_ - cb->buf_);
    if (*sz == 0) {
        return NULL;
    }
    return  cb->buf_;
}

uint8_t* cb_suffix(char_buffer* cb, int* sz) {
    const uint8_t* end = cb->buf_ + cb->size_;
    *sz = end - cb->cend_;
    if (*sz == 0) {
        return NULL;
    }
    return cb->cend_;
}

int cb_copy(char_buffer* cb, uint8_t* buf, int size) {
    const int prefix = cb->curr_ - cb->buf_;
    int used;
    for (used = 0; used < size && used < prefix; used++) {
        buf[used] = cb->buf_[used];
    }

    const int suffix = (cb->buf_+cb->size_) - cb->cend_;
    for (int i = 0; i < suffix && used < size; used++,i++) {
        buf[used] = cb->cend_[i];
    }

    return used;
}
