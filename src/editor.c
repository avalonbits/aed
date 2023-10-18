#include "editor.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cmd_ops.h"

editor* ed_init(editor* ed, int mem_kb, uint8_t cursor) {
    if (!tb_init(&ed->buf_, mem_kb)) {
       return NULL;
    }
    scr_init(&ed->scr_, cursor, DEFAULT_FG, DEFAULT_BG);
    return ed;
}

void ed_destroy(editor* ed) {
    scr_destroy(&ed->scr_);
    tb_destroy(&ed->buf_);
}

key_command read_input();

void ed_run(editor* ed) {
    screen* scr = &ed->scr_;
    text_buffer* buf = &ed->buf_;

    for (;;) {
        scr_footer(scr, tb_xpos(buf), tb_ypos(buf));
        key_command kc = read_input();
        if (kc.cmd == CMD_QUIT) {
            break;
        }
        cmds[kc.cmd](scr, buf, kc);
    }
    scr_clear(scr);
    vdp_clear_screen();

    printf("Buf contents:\r\n");
    int sz = tb_used(buf);
    for (int i = 0; i < sz; i++) {
        outchar(tb_peek_at(buf, i));
    }
}

key_command ctrlCmds(key_command kc) {
    if (kc.vkey == VK_q || kc.vkey == VK_Q) {
        kc.cmd = CMD_QUIT;
    } else {
        kc.cmd = CMD_NOOP;
    }
    return kc;
}

key_command editCmds(key_command kc) {
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
        case VK_DELETE:
        case VK_KP_DELETE:
            kc.cmd = CMD_DEL;
            break;
        case VK_HOME:
        case VK_KP_HOME:
            kc.cmd = CMD_HOME;
            break;
        case VK_END:
        case VK_KP_END:
            kc.cmd = CMD_END;
            break;
        case VK_RETURN:
        case VK_KP_ENTER:
            kc.cmd = CMD_NEWL;
            break;
        case VK_UP:
        case VK_KP_UP:
            kc.cmd = CMD_UP;
            break;
        case VK_DOWN:
        case VK_KP_DOWN:
            kc.cmd = CMD_DOWN;
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
        return ctrlCmds(kc);
    }

    if (kc.key != 0x7F && kc.key >= 32) {
        kc.cmd = CMD_PUTC;
    } else {
        return editCmds(kc);
    }

    return kc;
}

