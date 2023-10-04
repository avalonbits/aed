#ifndef _GAP_BUFFER_H_
#define _GAP_BUFFER_H_

typedef unsigned char CHAR;

typedef struct _gap_buffer  {
    CHAR* buf_;
    int size_;
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
void put(gap_buffer* gb, CHAR ch);
void del(gap_buffer* gb);
void backspace(gap_buffer* gb);

// Cursor ops.
int next(gap_buffer* gb, int cnt);
int prev(gap_buffer* gb, int cnt);


#endif  // _GAP_BUFFER_H_
