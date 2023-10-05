#ifndef _EDITOR_H_
#define _EDITOR_H_

#include <stdint.h>

#include "screen.h"
#include "gap_buffer.h"

typedef struct _editor {
    screen* scr_;
    gap_buffer* buf_;
} editor;

editor* ed_init(editor* ed, screen* scr, gap_buffer* gb, int mem_kb, uint8_t cursor);
void ed_destroy(editor* ed);

void ed_run(editor* ed);

#endif  // _EDITOR_H_
