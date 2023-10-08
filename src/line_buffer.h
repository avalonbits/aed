#ifndef _LINEBUFFER_H_
#define _LINEBUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _line_buffer  {
    int size_;
    uint8_t* buf_;
    uint8_t* curr_;
    uint8_t* cend_;
} line_buffer;

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size);
void lb_destroy(line_buffer* lb);

// Info ops
int lb_size(line_buffer* lb);
int lb_available(line_buffer* lb);
int lb_used(line_buffer* lb);

// Line ops.
bool lb_linc(line_buffer* lb);
bool lb_ldec(line_buffer* lb);
int lb_lcur(line_buffer* lb);
int lb_lmax(line_buffer* lb);
int lb_lused(line_buffer* lb);

// Cursor ops.
bool lb_up(line_buffer* lb);
bool lb_down(line_buffer* lb);
bool lb_new(line_buffer* lb);

#endif  // _LINEBUFFER_H_
