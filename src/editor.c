#include "editor.h"

#include <mos_api.h>
#include <stdbool.h>

editor* ed_init(editor* ed, screen* scr, gap_buffer* gb, int mem_kb, char cursor) {
    if (!gb_init(gb, mem_kb << 10)) {
       return NULL;
    }
    scr_init(scr, cursor, DEFAULT_FG, DEFAULT_BG);
    ed->scr_ = scr;
    ed->buf_ = gb;
    return ed;
}

void ed_destroy(editor* ed) {
    ed->scr_ = NULL;
    ed->buf_ = NULL;
}

typedef enum _command {
    CMD_NOP = 0,
    CMD_PUTC = 1,
    CMD_QUIT = 2,
} Command;

typedef struct _key_command {
    Command cmd;
    char key;
} key_command;

key_command read_input();

void ed_run(editor* ed) {
    screen* scr = ed->scr_;

    bool done = false;
    while (!done) {
        key_command kc = read_input();
        switch (kc.cmd) {
            case CMD_QUIT:
                done = true;
                break;
            case CMD_PUTC:
                scr_putc(scr, kc.key);
                break;
            case CMD_NOP:
                break;
        }
    }
}

key_command read_input() {
    key_command kc = {CMD_NOP, ' '};
    kc.key = getch();

    if (kc.key == 'q') {
        kc.cmd = CMD_QUIT;
    } else if (kc.key != 0x7F && kc.key >= 32) {
        kc.cmd = CMD_PUTC;
    }

    return kc;
}

