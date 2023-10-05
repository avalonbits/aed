#include "editor.h"

#include <mos_api.h>
#include <stdbool.h>
#include <stdio.h>

#include "vkey.h"

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
    VKey vkey;
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

key_command ctrlCmds(key_command kc) {
    if (kc.vkey == VK_q || kc.vkey == VK_Q) {
        kc.cmd = CMD_QUIT;
    } else {
        kc.cmd = CMD_NOP;
    }
    return kc;
}

key_command read_input() {
    key_command kc = {CMD_NOP, ' ', VK_NONE};
    kc.key = getch();
    kc.vkey = getsysvar_vkeycode();

    const uint8_t mods = getsysvar_keymods();
    if ((mods & MOD_CTRL)) {
        return ctrlCmds(kc);
    }

    if (kc.key != 0x7F && kc.key >= 32) {
        kc.cmd = CMD_PUTC;
    } else {
    }

    return kc;
}

