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

#include "screen.h"
#include "text_buffer.h"
#include "user_input.h"

typedef struct _editor {
    screen scr_;
    text_buffer buf_;
    user_input ui_;
    bool select_;
} editor;

editor* ed_init(editor* ed, int mem_kb, const char* fname);
void ed_destroy(editor* ed);

void ed_run(editor* ed);

#endif  // _EDITOR_H_
