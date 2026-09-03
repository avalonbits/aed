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

#ifndef _EDITOR_H_
#define _EDITOR_H_

#include <stdint.h>

#include "cmd_ops.h"
#include "screen.h"
#include "text_buffer.h"
#include "user_input.h"

typedef struct _editor {
    screen scr_;
    text_buffer buf_;
    user_input ui_;
} editor;

editor* ed_init(editor* ed, int mem_kb, const char* fname);
void ed_destroy(editor* ed);

void ed_run(editor* ed);

// Three commands the main loop handles itself rather than calling through, so
// they are sentinels in the table rather than functions.
#define CMD_PUTC    (cmd_op) 0x01
#define CMD_QUIT    (cmd_op) 0x02
#define CMD_SAVE    (cmd_op) 0x03

typedef struct _key_command {
    cmd_op cmd;
    key k;
} key_command;

// What a key means with CTRL held. Declared here so the bindings can be
// asserted directly: a command that exists but is not reachable from the
// keyboard is not a feature, and nothing below this level would notice.
key_command ctrlCmds(key_command kc, char mods);

#endif  // _EDITOR_H_
