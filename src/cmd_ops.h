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

#ifndef _CMD_OPS_H_
#define _CMD_OPS_H_

#include "text_buffer.h"
#include "screen.h"
#include "vkey.h"

typedef struct _key {
    uint8_t key;
    VKey vkey;
} key;

typedef void(*cmd_op)(screen*, text_buffer*);

void cmd_show(screen* scr, text_buffer* buf);
bool cmd_quit(screen* scr, text_buffer* buf);

void cmd_noop(screen* scr, text_buffer* buf);
void cmd_putc(screen* scr, text_buffer* buf, key k);
void cmd_del(screen* scr, text_buffer* buf);
void cmd_bksp(screen* scr, text_buffer* buf);
void cmd_newl(screen* scr, text_buffer* buf);
void cmd_del_line(screen* scr, text_buffer* buf);
void cmd_left(screen* scr, text_buffer* buf);
void cmd_w_left(screen* scr, text_buffer* buf);
void cmd_w_right(screen* scr, text_buffer* buf);
void cmd_right(screen* scr, text_buffer* buf);
void cmd_up(screen* scr, text_buffer* buf);
void cmd_down(screen* scr, text_buffer* buf);
void cmd_home(screen* scr, text_buffer* buf);
void cmd_end(screen* scr, text_buffer* buf);

#endif  // _CMD_OPS_H_
