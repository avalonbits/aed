#ifndef _LINE_BUFFER_H_
#define _LINE_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _line_buffer  {
    int size_;
    int* buf_;
    int* curr_;
    int* cend_;
} line_buffer;

// Setup ops.
line_buffer* lb_init(line_buffer* lb, int size);
void lb_destroy(line_buffer* lb);

// Info ops
int lb_curr(line_buffer* lb);
int lb_avai(line_buffer* lb);
int lb_max(line_buffer* lb);
bool lb_last(line_buffer* lb);

// Line ops.
bool lb_cinc(line_buffer* lb);
bool lb_cdec(line_buffer* lb);
int lb_csize(line_buffer* lb);

// Cursor ops.
bool lb_up(line_buffer* lb);
bool lb_down(line_buffer* lb);
bool lb_new(line_buffer* lb, int size);
bool lb_del(line_buffer* lb);

int lb_copy(line_buffer* cb, uint8_t* buf, int size);

#endif  // _LINE_BUFFER_H_
