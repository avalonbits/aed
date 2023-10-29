#ifndef _CHAR_BUFFER_H_
#define _CHAR_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _char_buffer  {
    int size_;
    uint8_t* buf_;
    uint8_t* curr_;
    uint8_t* cend_;
} char_buffer;

// Setup ops.
char_buffer* cb_init(char_buffer* cb, int size);
void cb_destroy(char_buffer* cb);

// Info ops.
int cb_size(char_buffer* cb);
int cb_available(char_buffer* cb);
int cb_used(char_buffer* cb);

// Characterr ops.
void cb_put(char_buffer* cb, uint8_t ch);
bool cb_del(char_buffer* cb);
bool cb_bksp(char_buffer* cb);

// Cursor ops.
uint8_t cb_next(char_buffer* cb, int cnt);
uint8_t cb_prev(char_buffer* cb, int cnt);

// Char read.
uint8_t cb_peek(char_buffer* cb);
uint8_t cb_peek_at(char_buffer* cb, int idx);
int cb_copy(char_buffer* cb, uint8_t* buf, int size);
uint8_t* cb_prefix(char_buffer* cb, int* sz);
uint8_t* cb_suffix(char_buffer* cb, int* sz);


#endif  // _CHAR_BUFFER_H_
