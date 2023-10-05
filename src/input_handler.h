#ifndef _INPUT_HANDLER_H_
#define _INPUT_HANDLER_H_

typedef enum _command {
    CMD_NOP = 0,
    CMD_PUTC = 1,
} Command;

typedef struct _key_command {
    Command cmd;
    char key;
} key_command;

key_command read_input();

#endif  // _INPUT_HANDLER_H_
