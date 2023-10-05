#include "editor.h"

#include <mos_api.h>
#include <stdbool.h>
#include <stdio.h>

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
    uint8_t vkey;

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

#define CTRL_KEY 0x01
#define VKEY_q 0x26
#define VKEY_Q 0x40
key_command ctrlCmds(key_command kc) {
    if (kc.vkey == VKEY_q || kc.vkey == VKEY_Q) {
        kc.cmd = CMD_QUIT;
    } else {
        kc.cmd = CMD_NOP;
    }
    return kc;
}

key_command read_input() {
    key_command kc = {CMD_NOP, ' '};
    kc.key = getch();
    kc.vkey = getsysvar_vkeycode();

    const uint8_t mods = getsysvar_keymods();
    if ((mods & CTRL_KEY)) {
        return ctrlCmds(kc);
    }

    if (kc.key != 0x7F && kc.key >= 32) {
        kc.cmd = CMD_PUTC;
    } else {
    }

    return kc;
}

