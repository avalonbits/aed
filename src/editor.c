#include "editor.h"

#include <mos_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cmd_ops.h"

editor* ed_init(editor* ed, screen* scr, char_buffer* cb, int mem_kb, uint8_t cursor) {
    if (!cb_init(cb, mem_kb << 10)) {
       return NULL;
    }
    scr_init(scr, cursor, DEFAULT_FG, DEFAULT_BG);
    ed->scr_ = scr;
    ed->buf_ = cb;
    return ed;
}

void ed_destroy(editor* ed) {
    ed->scr_ = NULL;
    ed->buf_ = NULL;
}

key_command read_input();

void ed_run(editor* ed) {
    screen* scr = ed->scr_;
    char_buffer* buf = ed->buf_;

    for (;;) {
        key_command kc = read_input();
        if (kc.cmd == CMD_QUIT) {
            break;
        }
        cmds[kc.cmd](scr, buf, kc);
    }
    scr_clear(scr);

    printf("Buf contents:\r\n");
    int sz = cb_used(buf);
    for (int i = 0; i < sz; i++) {
        outchar(cb_peek_at(buf, i));
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
        case VK_RIGHT:
        case VK_KP_RIGHT:
            kc.cmd = CMD_RGHT;
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

