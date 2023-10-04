#ifndef _GAP_BUFFER_H_
#define _GAP_BUFFER_H_

typedef unsigned char CHAR;

typedef struct _gap_buffer  {
    int size_;
    CHAR* buf_;
    CHAR* curr_;
    CHAR* cend_;
} gap_buffer;

// Setup ops.
gap_buffer* gb_init(gap_buffer* gb, int size);
void gb_destroy(gap_buffer* gb);

// Info ops.
int gb_size(gap_buffer* gb);
int gb_available(gap_buffer* gb);
int gb_used(gap_buffer* gb);

// Characterr ops.
void gb_put(gap_buffer* gb, CHAR ch);
void gb_del(gap_buffer* gb);
void gb_bksp(gap_buffer* gb);

// Cursor ops.
char gb_next(gap_buffer* gb, int cnt);
char gb_prev(gap_buffer* gb, int cnt);

// Char read.
CHAR gb_peek(gap_buffer* gb);
CHAR gb_peek_at(gap_buffer* gb, int idx);
int gb_copy(gap_buffer* gb, CHAR* buf, int size);


#endif  // _GAP_BUFFER_H_
