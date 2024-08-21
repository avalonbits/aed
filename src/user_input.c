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

#include "user_input.h"

#include <agon/vdp_vdu.h>
#include <mos_api.h>
#include <string.h>

#include "vkey.h"

user_input* ui_init(user_input* ui, int size, char ypos, char cols) {
    if (!cb_init(&ui->cb_, size)) {
        return NULL;
    }
    ui->ypos_ = ypos;
    ui->cols_ = cols;
    return ui;
}

void ui_destroy(user_input* ui) {
    cb_destroy(&ui->cb_);
}


static char goto_line[11] = "Goto line: ";
static int atoi(char* str, char sz) {
    int v = 0;
    int mul = 1;
    for (char i = 1; i <= sz; i++) {
        v += (str[sz-i] - '0') * mul;
        mul *= 10;
    }
    return v;
}

RESPONSE ui_goto(user_input* ui, screen* scr, int* line) {
    scr_write_line(scr, ui->ypos_, goto_line, sizeof(goto_line));
    vdp_cursor_tab(sizeof(goto_line), ui->ypos_);
    scr_show_cursor_ch(scr, scr->cursor_);

    char_buffer* cb = &ui->cb_;
    cb_clear(cb);

    do {
        char key = getch();
        VKey vkey = getsysvar_vkeycode();

        if (key >= '0' && key <= '9') {
            cb_put(cb, key);
            putch(key);
            scr_show_cursor_ch(scr, cb_peek(cb));
            continue;
        }

        switch (vkey) {
            case VK_ESCAPE:
                return CANCEL_OPT;
            case VK_RETURN: {
                int sz = 0;
                char* buf = cb_prefix(cb, &sz);
                if (sz <= 0) {
                    return CANCEL_OPT;
                }
                *line = atoi(buf, (char) sz);
                if (*line < 0) {
                    return CANCEL_OPT;
                }
                return YES_OPT;
            }
            case VK_BACKSPACE:
                scr_hide_cursor_ch(scr, cb_peek(cb));
                if (cb_bksp(cb)) {
                    vdp_cursor_left();
                }
                scr_show_cursor_ch(scr, cb_peek(cb));
                break;
            default:
                break;

        }
    } while (true);

    return CANCEL_OPT;
}


static const char col_select[39] = "Use UP/DOWN LEFT/RIGHT to select FG/BG";

RESPONSE ui_color_picker(user_input* ui, screen* scr) {
    char fg = scr->fg_;
    char bg = scr->bg_;

    const int pad =  (scr->cols_ - sizeof(col_select)) / 2;
    do {
        vdp_cursor_tab(0, ui->ypos_);
        set_colours(fg, bg);
        for (int i = 0; i < pad; i++) {
            putch(' ');
        }
        VDP_PUTS(col_select);
        for (int i = 0; i <= pad; i++) {
            putch(' ');
        }

        getch();
        VKey vkey = getsysvar_vkeycode();

        switch (vkey) {
            case VK_ESCAPE:
                return CANCEL_OPT;
            case VK_UP:
            case VK_KP_UP:
                fg = (fg + 1) % scr->colors_;
                break;
            case VK_DOWN:
            case VK_KP_DOWN:
                if (fg == 0) {
                    fg = scr->colors_-1;
                } else {
                    fg = (fg - 1) % scr->colors_;
                }
                break;
            case VK_LEFT:
            case VK_KP_LEFT:
                if (bg == 0) {
                    bg = scr->colors_-1;
                } else {
                    bg = (bg - 1) % scr->colors_;
                }
                break;
            case VK_RIGHT:
            case VK_KP_RIGHT:
                bg = (bg + 1) % scr->colors_;
                break;
            case VK_RETURN:
            case VK_KP_ENTER:
                scr->fg_ = fg;
                scr->bg_ = bg;
                return YES_OPT;
            default:
                break;
        }

    } while (true);

    return CANCEL_OPT;
}

static const char options[13] = " [Y/N/ESC]: ";

RESPONSE ui_dialog(user_input* ui, screen* scr, char* msg) {
    const int msz = strlen(msg);
    scr_write_line(scr, ui->ypos_, msg, msz);
    vdp_cursor_tab(msz, ui->ypos_);
    VDP_PUTS(options);
    scr_show_cursor_ch(scr, scr->cursor_);

    do {
        char key = getch();
        VKey vkey = getsysvar_vkeycode();
        if (vkey == VK_ESCAPE) {
            break;
        }
        if (key == 'Y' || key == 'y') {
            return YES_OPT;
        }
        if (key == 'N' || key == 'n') {
            return NO_OPT;
        }
    } while (true);

    return CANCEL_OPT;
}

RESPONSE ui_text(
    user_input* ui,
    screen* scr,
    char* title,
    char* prefill,
    char** buf,
    int* sz
) {
    const int msz = strlen(title);
    scr_write_line(scr, ui->ypos_, title, msz);
    vdp_cursor_tab(msz, ui->ypos_);

    *buf = NULL;
    *sz = 0;
    char_buffer* cb = &ui->cb_;
    cb_clear(cb);

    if (prefill != NULL) {
        for (unsigned int i = 0; i < strlen(prefill); i++) {
            const char ch = prefill[i];
            cb_put(cb, ch);
            putch(ch);
        }
    }
    scr_show_cursor_ch(scr, scr->cursor_);

    do {
        char key = getch();
        VKey vkey = getsysvar_vkeycode();

        if (key != 0x7F && key > 0x20) {
            cb_put(cb, key);
            putch(key);
            scr_show_cursor_ch(scr, cb_peek(cb));
            continue;
        }

        switch (vkey) {
            case VK_ESCAPE:
                return CANCEL_OPT;
            case VK_RETURN:
            case VK_KP_ENTER:
                if (cb_used(cb) == 0) {
                    return CANCEL_OPT;
                } else {
                    *buf = cb_prefix(cb, sz);
                    return YES_OPT;
                }
                break;
            case VK_BACKSPACE:
                scr_hide_cursor_ch(scr, cb_peek(cb));
                if (cb_bksp(cb)) {
                    vdp_cursor_left();
                }
                scr_show_cursor_ch(scr, cb_peek(cb));
                break;
            default:
                break;
        }
    } while (true);

    return CANCEL_OPT;
}

