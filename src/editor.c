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

#define CMD_QUIT NULL
typedef struct _key_command {
    cmd_op cmd;
    key k;
} key_command;

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
        kc.cmd(scr, buf, kc.k);
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
    switch (kc.k.vkey) {
        case VK_q:
        case VK_Q:
            kc.cmd = CMD_QUIT;
            break;
        case VK_LEFT:
        case VK_KP_LEFT:
            kc.cmd = cmd_w_left;
            break;
        default:
            kc.cmd = cmd_noop;
            break;
    }
    return kc;
}

key_command editCmds(key_command kc) {
    switch (kc.k.vkey) {
        case VK_LEFT:
        case VK_KP_LEFT:
            kc.cmd = cmd_left;
            break;
        case VK_RIGHT:
        case VK_KP_RIGHT:
            kc.cmd = cmd_right;
            break;
        case VK_BACKSPACE:
            kc.cmd = cmd_bksp;
            break;
        case VK_DELETE:
        case VK_KP_DELETE:
            kc.cmd = cmd_del;
            break;
        case VK_HOME:
        case VK_KP_HOME:
            kc.cmd = cmd_home;
            break;
        case VK_END:
        case VK_KP_END:
            kc.cmd = cmd_end;
            break;
        case VK_RETURN:
        case VK_KP_ENTER:
            kc.cmd = cmd_newl;
            break;
        case VK_UP:
        case VK_KP_UP:
            kc.cmd = cmd_up;
            break;
        case VK_DOWN:
        case VK_KP_DOWN:
            kc.cmd = cmd_down;
            break;
        default:
            break;
    }
    return kc;
}

key_command read_input() {
    key_command kc = {cmd_noop, {'\0', VK_NONE}};
    kc.k.key = getch();
    kc.k.vkey = getsysvar_vkeycode();

    const uint8_t mods = getsysvar_keymods();
    if ((mods & MOD_CTRL)) {
        return ctrlCmds(kc);
    }

    if (kc.k.key != 0x7F && kc.k.key >= 32) {
        kc.cmd = cmd_putc;
    } else {
        return editCmds(kc);
    }

    return kc;
}

