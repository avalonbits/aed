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

#define DEFAULT_CURSOR 32

editor* ed_init(editor* ed, int mem_kb, const char* fname) {
    screen* scr = scr_init(&ed->scr_, DEFAULT_CURSOR);
    if (!tb_init(&ed->buf_, scr->tab_size_, mem_kb, fname)) {
       return NULL;
    }
    if (!ui_init(&ed->ui_, 256, ed->scr_.bottomY_, ed->scr_.cols_)) {
        tb_destroy(&ed->buf_);
        return NULL;
    }

    if (tb_used(&ed->buf_) > 0) {
        cmd_show(ed);
    }
    ed->select_ = false;
    ed->prev_select_ = false;
    return ed;
}

void ed_destroy(editor* ed) {
    ui_destroy(&ed->ui_);
    scr_destroy(&ed->scr_);
    tb_destroy(&ed->buf_);
}

#define CMD_PUTC    (cmd_op) 0x01
#define CMD_QUIT    (cmd_op) 0x02
#define CMD_SAVE    (cmd_op) 0x03
typedef struct _key_command {
    cmd_op cmd;
    key k;
    bool select;

} key_command;

key_command read_input();

static void set_mode(editor* ed, bool select) {
    ed->prev_select_ = ed->select_;
    ed->select_ = select;
    const bool edit_to_select = ed->select_ && !ed->prev_select_;
    const bool select_to_edit = ed->prev_select_ && !ed->select_;

    if (edit_to_select || select_to_edit) {
        // We are moving between modes. Swap fg/bg colors.
        screen* scr = &ed->scr_;
        const char tmp = scr->fg_;

        scr->fg_ = scr->bg_;
        scr->bg_ = tmp;
        set_colours(scr->fg_, scr->bg_);

        if (select_to_edit) {
            // We moving from select to edit mode. Refresh the screen.
            refresh_screen(scr, &ed->buf_);
        }
    }
}

void ed_run(editor* ed) {
    text_buffer* buf = &ed->buf_;
    screen* scr = &ed->scr_;

    scr_footer(scr, tb_fname(buf), false, tb_changed(buf), tb_xpos(buf), tb_ypos(buf));

    for (;;) {
        key_command kc = read_input();

        set_mode(ed, kc.select);

        if (kc.cmd == CMD_PUTC) {
            cmd_putc(ed, kc.k);
        } else if (kc.cmd == CMD_QUIT) {
            if (cmd_quit(ed)) {
                break;
            }
        } else if (kc.cmd == CMD_SAVE) {
            cmd_save(ed);
        } else if (kc.cmd != NULL) {
            kc.cmd(ed);
        }

        scr_footer(scr, tb_fname(buf), kc.select, tb_changed(buf), tb_xpos(buf), tb_ypos(buf));
    }
    vdp_clear_screen();
}

key_command ctrlCmds(key_command kc, char mods) {
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
        case VK_S:
        case VK_s:
		    if (mods & MOD_ALT) {
                kc.cmd = cmd_save_as;
            } else {
                kc.cmd = CMD_SAVE;
            }
            break;
        case VK_C:
        case VK_c:
            if (mods & MOD_ALT) {
                kc.cmd = cmd_color_picker;
            }
            break;
        case VK_G:
        case VK_g:
            kc.cmd = cmd_goto;
            break;
        default:
            kc.cmd = NULL;
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
        case VK_PAGEUP:
            kc.cmd = cmd_page_up;
            break;
        case VK_PAGEDOWN:
            kc.cmd = cmd_page_down;
            break;
        default:
            break;
    }
    return kc;
}

key_command read_input() {
    key_command kc = {NULL, {'\0', VK_NONE}, false};
    kc.k.key = getch();
    kc.k.vkey = getsysvar_vkeycode();

    const char mods = getsysvar_keymods();
    if (mods & MOD_CTRL) {
        kc.select = mods & MOD_SHFT;
        return ctrlCmds(kc, mods);
    }
    if (kc.k.key == '\t' || (kc.k.key != 0x7F && kc.k.key >= 32)) {
        kc.cmd = CMD_PUTC;
    } else {
        kc.select = mods & MOD_SHFT;
        return editCmds(kc);
    }

    return kc;
}

