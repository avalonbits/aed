/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "editor.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cmd_ops.h"
#include "line_buffer.h"

#define DEFAULT_CURSOR 32

editor* ed_init(editor* ed, int mem_kb, const char* fname) {
    if (!tb_init(&ed->buf_, mem_kb, fname)) {
       return NULL;
    }
    scr_init(&ed->scr_, DEFAULT_CURSOR);
    if (tb_used(&ed->buf_) > 0) {
        cmd_show(&ed->scr_, &ed->buf_);
    }
    return ed;
}

void ed_destroy(editor* ed) {
    scr_destroy(&ed->scr_);
    tb_destroy(&ed->buf_);
}

#define CMD_PUTC (cmd_op) 0x01
#define CMD_QUIT (cmd_op) 0x02

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
        if (kc.cmd == CMD_PUTC) {
            cmd_putc(scr, buf, kc.k);
        } else if (kc.cmd == CMD_QUIT && cmd_quit(scr, buf)) {
            break;
        } else {
            kc.cmd(scr, buf);
        }
    }
    vdp_clear_screen();
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
        case VK_RIGHT:
        case VK_KP_RIGHT:
            kc.cmd = cmd_w_right;
            break;
        case VK_DELETE:
        case VK_KP_DELETE:
        case VK_d:
        case VK_D:
            kc.cmd = cmd_del_line;
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

    if (kc.k.key == '\t' || (kc.k.key != 0x7F && kc.k.key >= 32)) {
        kc.cmd = CMD_PUTC;
    } else {
        return editCmds(kc);
    }

    return kc;
}

