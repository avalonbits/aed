#ifndef _CMD_OPS_H_
#define _CMD_OPS_H_

#include "text_buffer.h"
#include "screen.h"
#include "vkey.h"

typedef enum _command {
    CMD_NOOP = 0,
    CMD_QUIT,

    CMD_PUTC,
    CMD_DEL,
    CMD_BKSP,
    CMD_LEFT,
    CMD_RGHT,
} Command;

typedef struct _key_command {
    Command cmd;
    uint8_t key;
    VKey vkey;
} key_command;

typedef void(*cmd_op)(screen*, text_buffer*, key_command);

extern cmd_op cmds[];

#endif  // _CMD_OPS_H_
