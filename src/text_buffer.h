#ifndef _TEXT_BUFFER_H_
#define _TEXT_BUFFER_H_

#include "char_buffer.h"
#include "line_buffer.h"

typedef struct _text_buffer {
    char_buffer cb_;
    line_buffer lb_;
    int x_;
    int y_;
} text_buffer;

text_buffer* tb_init(text_buffer* tb, int mem_kb);
void tb_destroy(text_buffer* tb);

// Info ops.
int tb_size(text_buffer* tb);
int tb_available(text_buffer* tb);
int tb_used(text_buffer* tb);

// Character ops.
void tb_put(text_buffer* tb, uint8_t ch);
bool tb_del(text_buffer* tb);
bool tb_bksp(text_buffer* tb);
bool tb_newline(text_buffer* tb);

// Cursor ops.
uint8_t tb_next(text_buffer* tb);
uint8_t tb_prev(text_buffer* tb);
uint8_t tb_home(text_buffer* tb);
void tb_end(text_buffer* tb);
int tb_xpos(text_buffer* tb);
int tb_ypos(text_buffer* tb);

// Char read.
uint8_t tb_peek(text_buffer* tb);
uint8_t tb_peek_at(text_buffer* tb, int idx);
int tb_copy(text_buffer* tb, uint8_t* buf, int sz);
uint8_t* tb_suffix(text_buffer* tb, int* sz);


#endif // _TEXT_BUFFER_H_
