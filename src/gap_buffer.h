#ifndef _GAP_BUFFER_H_
#define _GAP_BUFFER_H_

typedef struct _gap_buffer  {
    unsigned char* buf_;
    int size_;
    unsigned char* curr_;
    unsigned char* cend_;
} gap_buffer;

gap_buffer* gb_init(gap_buffer* gb, int size);
void gb_destroy(gap_buffer* gb);

#endif  // _GAP_BUFFER_H_
