#ifndef _GAP_BUFFER_H_
#define _GAP_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _gap_buffer  {
    int size_;
    uint8_t* buf_;
    uint8_t* curr_;
    uint8_t* cend_;
} gap_buffer;

// Setup ops.
gap_buffer* gb_init(gap_buffer* gb, int size);
void gb_destroy(gap_buffer* gb);

// Info ops.
int gb_size(gap_buffer* gb);
int gb_available(gap_buffer* gb);
int gb_used(gap_buffer* gb);

// Characterr ops.
void gb_put(gap_buffer* gb, uint8_t ch);
void gb_del(gap_buffer* gb);
bool gb_bksp(gap_buffer* gb);

// Cursor ops.
uint8_t gb_next(gap_buffer* gb);
uint8_t gb_prev(gap_buffer* gb);

// Char read.
uint8_t gb_peek(gap_buffer* gb);
uint8_t gb_peek_at(gap_buffer* gb, int idx);
int gb_copy(gap_buffer* gb, uint8_t* buf, int size);
uint8_t* gb_suffix(gap_buffer* gb, int* sz);


#endif  // _GAP_BUFFER_H_
