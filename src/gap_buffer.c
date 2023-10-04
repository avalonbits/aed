#include "gap_buffer.h"

#include <stdlib.h>

gap_buffer* gb_init(gap_buffer* gb, int size) {
    if (gb == NULL) {
        return NULL;
    }

    gb->buf_ = (unsigned char*) malloc(sizeof(unsigned char) * size);
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
