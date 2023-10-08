#ifndef _EDITOR_H_
#define _EDITOR_H_

#include <stdint.h>

#include "screen.h"
#include "char_buffer.h"

typedef struct _editor {
    screen* scr_;
    char_buffer* buf_;
} editor;

editor* ed_init(editor* ed, screen* scr, char_buffer* cb, int mem_kb, uint8_t cursor);
void ed_destroy(editor* ed);

void ed_run(editor* ed);

#endif  // _EDITOR_H_
