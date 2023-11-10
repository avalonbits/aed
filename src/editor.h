#ifndef _EDITOR_H_
#define _EDITOR_H_

#include <stdint.h>

#include "screen.h"
#include "text_buffer.h"

typedef struct _editor {
    screen scr_;
    text_buffer buf_;
} editor;

editor* ed_init(editor* ed, int mem_kb, const char* fname);
void ed_destroy(editor* ed);

void ed_run(editor* ed);

#endif  // _EDITOR_H_
