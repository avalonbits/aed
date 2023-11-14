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

#ifndef _TEXT_BUFFER_H_
#define _TEXT_BUFFER_H_

#include "char_buffer.h"
#include "line_buffer.h"

typedef struct _text_buffer {
    char_buffer cb_;
    line_buffer lb_;
    int x_;

    char fname_[256];
} text_buffer;

text_buffer* tb_init(text_buffer* tb, int mem_kb, const char* fname);
void tb_destroy(text_buffer* tb);

// Info ops.
int tb_size(text_buffer* tb);
int tb_available(text_buffer* tb);
int tb_used(text_buffer* tb);
bool tb_eol(text_buffer* tb);
bool tb_bol(text_buffer* tb);

// Character ops.
void tb_put(text_buffer* tb, uint8_t ch);
bool tb_del(text_buffer* tb);
bool tb_bksp(text_buffer* tb);
bool tb_newline(text_buffer* tb);
bool tb_del_line(text_buffer* tb);
bool tb_del_merge(text_buffer* tb);
bool tb_bksp_merge(text_buffer* tb);

// Cursor ops.
uint8_t tb_next(text_buffer* tb);
uint8_t tb_w_next(text_buffer* tb);
uint8_t tb_prev(text_buffer* tb);
uint8_t tb_w_prev(text_buffer* tb);
uint8_t tb_home(text_buffer* tb);
uint8_t tb_up(text_buffer* tb);
uint8_t tb_down(text_buffer* tb);
uint8_t tb_end(text_buffer* tb);
int tb_xpos(text_buffer* tb);
int tb_ypos(text_buffer* tb);

// Char read.
uint8_t tb_peek(text_buffer* tb);
uint8_t tb_peek_at(text_buffer* tb, int idx);
uint8_t* tb_suffix(text_buffer* tb, int* sz);
uint8_t* tb_prefix(text_buffer* tb, int* sz);

bool tb_load(text_buffer* tb, const char* fname);
void tb_content(text_buffer* tb, uint8_t** prefix, int* psz, uint8_t** suffix, int* ssz);
bool tb_valid_file(text_buffer* tb);

// Line read
typedef struct _line {
    uint8_t* b;
    int sz;
    int osz;
} line;
typedef line(*line_itr)();

line_itr tb_pline(text_buffer* buf);
line_itr tb_nline(text_buffer* buf, int from_l);

#endif // _TEXT_BUFFER_H_
