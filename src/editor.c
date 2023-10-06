#include "editor.h"

#include <mos_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vkey.h"

editor* ed_init(editor* ed, screen* scr, gap_buffer* gb, int mem_kb, uint8_t cursor) {
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
    CMD_NOOP,
    CMD_PUTC,
    CMD_QUIT,
    CMD_BKSP,
    CMD_LEFT,
} Command;

typedef struct _key_command {
    Command cmd;
    uint8_t key;
    VKey vkey;
} key_command;

key_command read_input();

void ed_run(editor* ed) {
    screen* scr = ed->scr_;
    gap_buffer* buf = ed->buf_;

    bool done = false;
    while (!done) {
        key_command kc = read_input();
        switch (kc.cmd) {
            case CMD_NOOP:
                break;
            case CMD_QUIT:
                done = true;
                break;
            case CMD_PUTC:
                scr_putc(scr, kc.key);
				gb_put(buf, kc.key);
                break;
            case CMD_BKSP:
                break;
            case CMD_LEFT:
                break;
        }
    }
    scr_clear(scr);

    printf("Buf contents:\r\n");
    int sz = gb_used(buf);
    for (int i = 0; i < sz; i++) {
        outchar(gb_peek_at(buf, i));
    }
}

key_command modCmds(key_command kc) {
    if (kc.vkey == VK_q || kc.vkey == VK_Q) {
        kc.cmd = CMD_QUIT;
    } else {
        kc.cmd = CMD_NOOP;
    }
    return kc;
}

key_command ctrlCmds(key_command kc) {
    switch (kc.vkey) {
        case VK_LEFT:
        case VK_KP_LEFT:
            kc.cmd = CMD_LEFT;
            break;
        case VK_BACKSPACE:
            kc.cmd = CMD_BKSP;
            break;
        default:
            break;
    }
    return kc;
}

key_command read_input() {
    key_command kc = {CMD_NOOP, '\0', VK_NONE};
    kc.key = getch();
    kc.vkey = getsysvar_vkeycode();

    const uint8_t mods = getsysvar_keymods();
    if ((mods & MOD_CTRL)) {
        return modCmds(kc);
    }

    if (kc.key != 0x7F && kc.key >= 32) {
        kc.cmd = CMD_PUTC;
    } else {
        return ctrlCmds(kc);
    }

    return kc;
}

