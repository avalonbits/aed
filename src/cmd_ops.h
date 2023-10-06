#ifndef _CMD_OPS_H_
#define _CMD_OPS_H_

#include "gap_buffer.h"
#include "screen.h"
#include "vkey.h"

typedef enum _command {
    CMD_NOOP = 0,
    CMD_QUIT,

    CMD_PUTC,
    CMD_BKSP,
    CMD_LEFT,
} Command;

typedef struct _key_command {
    Command cmd;
    uint8_t key;
    VKey vkey;
} key_command;

typedef void(*cmd_op)(screen*, gap_buffer*, key_command);

extern cmd_op cmds[];

#endif  // _CMD_OPS_H_
