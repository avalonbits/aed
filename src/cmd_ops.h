#ifndef _CMD_OPS_H_
#define _CMD_OPS_H_

#include "text_buffer.h"
#include "screen.h"
#include "vkey.h"

typedef struct _key {
    uint8_t key;
    VKey vkey;
} key;

typedef void(*cmd_op)(screen*, text_buffer*);

void cmd_noop(screen* scr, text_buffer* buf);
void cmd_putc(screen* scr, text_buffer* buf, key k);
void cmd_del(screen* scr, text_buffer* buf);
void cmd_bksp(screen* scr, text_buffer* buf);
void cmd_newl(screen* scr, text_buffer* buf);
void cmd_left(screen* scr, text_buffer* buf);
void cmd_w_left(screen* scr, text_buffer* buf);
void cmd_w_right(screen* scr, text_buffer* buf);
void cmd_right(screen* scr, text_buffer* buf);
void cmd_up(screen* scr, text_buffer* buf);
void cmd_down(screen* scr, text_buffer* buf);
void cmd_home(screen* scr, text_buffer* buf);
void cmd_end(screen* scr, text_buffer* buf);

#endif  // _CMD_OPS_H_
